#include <imgui/imgui.h>

#include "core/atomic.h"
#include "core/hash.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/atomic.h"
#include "core/sync.h"
#include "core/thread.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "asset_compiler.h"
#include "editor/file_system_watcher.h"
#include "editor/log_ui.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/lua_wrapper.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "lz4/lz4.h"
#include <luacode.h>

// use this if you want to be able to use cached resources without having the original
// #define CACHE_MASTER


namespace Lumix
{


struct AssetCompilerImpl;


template<>
struct HashFunc<Path>
{
	static u32 get(const Path& key)
	{
		const u64 hash = key.getHash().getHashValue();
		return u32(hash ^ (hash >> 32));
	}
};


void AssetCompiler::IPlugin::addSubresources(AssetCompiler& compiler, const Path& path)
{
	const ResourceType type = compiler.getResourceType(path);
	if (!type.isValid()) return;
	
	compiler.addResource(type, path);
}


struct AssetCompilerImpl : AssetCompiler {
	struct CompileJob {
		u32 generation;
		Path path;
		bool compiled = false;
	};

	struct LoadHook : ResourceManagerHub::LoadHook {
		LoadHook(AssetCompilerImpl& compiler) : compiler(compiler) {}
		Action onBeforeLoad(Resource& res) override { return compiler.onBeforeLoad(res); }
		void loadRaw(const Path& requester, const Path& path) override { compiler.registerDependency(requester, path); }
		AssetCompilerImpl& compiler;
	};

	AssetCompilerImpl(StudioApp& app) 
		: m_app(app)
		, m_load_hook(*this)
		, m_allocator(app.getAllocator(), "asset compiler")
		, m_plugins(m_allocator)
		, m_to_compile(m_allocator)
		, m_compiled(m_allocator)
		, m_registered_extensions(m_allocator)
		, m_resources(m_allocator)
		, m_generations(m_allocator)
		, m_dependencies(m_allocator)
		, m_changed_files(m_allocator)
		, m_changed_dirs(m_allocator)
		, m_on_list_changed(m_allocator)
		, m_resource_compiled(m_allocator)
		, m_on_init_load(m_allocator)
	{
		Engine& engine = app.getEngine();
		FileSystem& fs = engine.getFileSystem();
		const char* base_path = fs.getBasePath();
		m_watcher = FileSystemWatcher::create(base_path, m_allocator);
		m_watcher->getCallback().bind<&AssetCompilerImpl::onFileChanged>(this);
		Path path(base_path, ".lumix/resources");
		if (!os::dirExists(path)) {
			if (!os::makePath(path.c_str())) logError("Could not create ", path);
			else {
				os::OutputFile file;
				path.append("/_version.bin");
				if (!file.open(path.c_str())) {
					logError("Could not open ", path);
				}
				else {
					file.write(0);
					file.close();
				}
			}
		}
		os::InputFile file;
		if (!file.open(".lumix/resources/_version.bin")) {
			logError("Could not open .lumix/resources/_version.bin");
		}
		else {
			u32 version;
			file.read(version);
			file.close();
			if (version != 0) {
				logWarning("Unsupported version of .lumix/resources. Rebuilding all assets.");
				os::FileIterator* iter = os::createFileIterator(".lumix/resources", m_allocator);
				os::FileInfo info;
				bool all_deleted = true;
				while (os::getNextFile(iter, &info)) {
					if (!info.is_directory) {
						const Path filepath(".lumix/resources/", info.filename);
						if (!os::deleteFile(filepath)) {
							all_deleted = false;
						}
					}
				}
				os::destroyFileIterator(iter);

				if (!all_deleted) {
					logError("Could not delete all files in .lumix/resources, please delete the directory and restart the editor.");
				}

				os::OutputFile out_file;
				if (!out_file.open(".lumix/resources/_version.bin")) {
					logError("Could not open .lumix/resources/_version.bin");
				}
				else {
					out_file.write(0);
					out_file.close();
				}
			}
		}

		ResourceManagerHub& rm = engine.getResourceManager();
		rm.setLoadHook(&m_load_hook);
	}

	~AssetCompilerImpl()
	{
		m_allocator.deallocate(m_lz4_state);
		os::OutputFile file;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (fs.open(".lumix/resources/_list.txt_tmp", file)) {
			file << "resources = {\n";
			for (const ResourceItem& ri : m_resources) {
				file << "\"" << ri.path << "\",\n";
			}
			file << "}\n\n";
			file << "dependencies = {\n";
			for (auto iter = m_dependencies.begin(), end = m_dependencies.end(); iter != end; ++iter) {
				file << "\t[\"" << iter.key() << "\"] = {\n";
				for (const Path& p : iter.value()) {
					file << "\t\t\"" << p << "\",\n";
				}
				file << "\t},\n";
			}
			file << "}\n";

			file.close();
			fs.deleteFile(".lumix/resources/_list.txt");
			fs.moveFile(".lumix/resources/_list.txt_tmp", ".lumix/resources/_list.txt");
		}
		else {
			logError("Could not save .lumix/resources/_list.txt");
		}

		ASSERT(m_plugins.empty());
		ResourceManagerHub& rm = m_app.getEngine().getResourceManager();
		rm.setLoadHook(nullptr);
	}
	
	void onBasePathChanged() override {
		Engine& engine = m_app.getEngine();
		FileSystem& fs = engine.getFileSystem();
		const char* base_path = fs.getBasePath();
		m_watcher = FileSystemWatcher::create(base_path, m_allocator);
		m_watcher->getCallback().bind<&AssetCompilerImpl::onFileChanged>(this);
		m_dependencies.clear();
		m_resources.clear();
		fillDB();
	}

	DelegateList<void(const Path&)>& listChanged() override {
		return m_on_list_changed;
	}

	DelegateList<void(Resource&, bool)>& resourceCompiled() override {
		return m_resource_compiled;
	}

	bool copyCompile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream tmp(m_allocator);
		if (!fs.getContentSync(src, tmp)) {
			logError("Failed to read ", src);
			return false;
		}

		ASSERT(tmp.size() < 0xffFFffFF);
		return writeCompiledResource(src, Span(tmp.data(), (u32)tmp.size()));
	}

	bool writeCompiledResource(const Path& path, Span<const u8> data) override {
		PROFILE_FUNCTION();
		jobs::enter(&m_lz4_mutex);
		constexpr u32 COMPRESSION_SIZE_LIMIT = 4096;
		OutputMemoryStream compressed(m_allocator);
		i32 compressed_size = 0;
		if (data.length() > COMPRESSION_SIZE_LIMIT) {
			if (!m_lz4_state) {
				m_lz4_state = (u8*)m_allocator.allocate(LZ4_sizeofState(), 8);
			}
			const i32 cap = LZ4_compressBound((i32)data.length());
			compressed.resize(cap);
			compressed_size = LZ4_compress_fast_extState(m_lz4_state, (const char*)data.begin(), (char*)compressed.getMutableData(), (i32)data.length(), cap, 1); 
			if (compressed_size == 0) {
				logError("Could not compress ", path);
				return false;
			}
			compressed.resize(compressed_size);
		}
		jobs::exit(&m_lz4_mutex);

		FileSystem& fs = m_app.getEngine().getFileSystem();
		const Path out_path(".lumix/resources/", path.getHash().getHashValue(), ".res");
		os::OutputFile file;
		if(!fs.open(out_path, file)) {
			logError("Could not create ", out_path);
			return false;
		}
		CompiledResourceHeader header;
		header.decompressed_size = data.length();
		if (data.length() > COMPRESSION_SIZE_LIMIT && compressed_size < i32(data.length() / 4 * 3)) {
			header.flags |= CompiledResourceHeader::COMPRESSED;
			(void)file.write(&header, sizeof(header));
			(void)file.write(compressed.data(), compressed_size);
		}
		else {
			(void)file.write(&header, sizeof(header));
			(void)file.write(data.begin(), data.length());
		}
		file.close();
		if (file.isError()) logError("Could not write ", out_path);
		return !file.isError();
	}

	static RuntimeHash dirHash(const Path& path) {
		StringView dir = Path::getDir(ResourcePath::getResource(path));
		if (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) dir.removeSuffix(1);
		return RuntimeHash(dir.begin, dir.size());
	}

	void addResource(ResourceType type, const Path& path) override {
		const FilePathHash hash = path.getHash();
		jobs::MutexGuard lock(m_resources_mutex);
		if (m_resources.find(hash).isValid()) {
			m_resources[hash] = {path, type, dirHash(path)};
		}
		else {
			m_resources.insert(hash, {path, type, dirHash(path)});
			m_on_list_changed.invoke(path);
		}
	}

	ResourceType getResourceType(StringView path) const override
	{
		StringView subres = ResourcePath::getSubresource(path);
		StringView ext = Path::getExtension(subres);

		alignas(u32) char tmp[6] = {};
		makeLowercase(Span(tmp), ext);
		if (strlen(tmp) >= 5) return INVALID_RESOURCE_TYPE;
		auto iter = m_registered_extensions.find(*(u32*)tmp);
		if (iter.isValid()) return iter.value();

		return INVALID_RESOURCE_TYPE;
	}


	bool acceptExtension(StringView ext, ResourceType type) const override
	{
		alignas(u32) char tmp[6] = {};
		makeLowercase(Span(tmp), ext);
		ASSERT(strlen(tmp) < 5);
		auto iter = m_registered_extensions.find(*(u32*)tmp);
		if (!iter.isValid()) return false;
		return iter.value() == type;
	}

	void registerExtension(const char* extension, ResourceType type) override
	{
		alignas(u32) char tmp[6] = {};
		makeLowercase(Span(tmp), extension);
		ASSERT(strlen(tmp) < 5);
		u32 q = *(u32*)tmp;
		ASSERT(!m_registered_extensions.find(q).isValid());

		m_registered_extensions.insert(q, type);
	}

	void addResource(const Path& fullpath) {
		char ext[10];
		copyString(Span(ext), Path::getExtension(fullpath));
		makeLowercase(Span(ext), ext);
		
		auto iter = m_plugins.find(RuntimeHash(ext));
		if (!iter.isValid()) return;

		iter.value()->addSubresources(*this, fullpath);
	}

	
	void processDir(StringView dir, u64 list_last_modified)
	{
		FileSystem& fs = m_app.getEngine().getFileSystem();
		auto* iter = fs.createFileIterator(dir);
		os::FileInfo info;
		while (getNextFile(iter, &info))
		{
			if (info.filename[0] == '.') continue;

			if (info.is_directory)
			{
				char child_path[MAX_PATH];
				copyString(child_path, dir);
				if(!dir.empty()) catString(child_path, "/");
				catString(child_path, info.filename);
				processDir(child_path, list_last_modified);
			}
			else
			{
				char fullpath[MAX_PATH];
				copyString(fullpath, dir);
				if(!dir.empty()) catString(fullpath, "/");
				catString(fullpath, info.filename);

				if (fs.getLastModified(fullpath[0] == '/' ? fullpath + 1 : fullpath) > list_last_modified) {
					addResource(Path(fullpath));
				}
				else {
					Path path(fullpath[0] == '/' ? fullpath + 1 : fullpath);
					if (!m_resources.find(path.getHash()).isValid()) {
						addResource(Path(fullpath));
					}
				}
			}
		}

		destroyFileIterator(iter);
	}


	void registerDependency(const Path& included_from, const Path& dependency) override
	{
		auto iter = m_dependencies.find(dependency);
		if (!iter.isValid()) {
			m_dependencies.insert(dependency, Array<Path>(m_allocator));
			iter = m_dependencies.find(dependency);
		}
		if (iter.value().indexOf(included_from) < 0) {
			iter.value().push(included_from);
		}
	}

	void fillDB() {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		const Path list_path(fs.getBasePath(), ".lumix/resources/_list.txt");
		OutputMemoryStream content(m_allocator);
		if (fs.getContentSync(Path(".lumix/resources/_list.txt"), content)) {
			lua_State* L = luaL_newstate();
			[&](){
				size_t bytecodeSize = 0;
				char* bytecode = luau_compile((const char*)content.data(), content.size(), NULL, &bytecodeSize);
				int res = luau_load(L, "lumix_asset_list", bytecode, bytecodeSize, 0);
				free(bytecode);
				if (res != 0) {
					logError(list_path, ": ", lua_tostring(L, -1));
					return;
				}

				if (lua_pcall(L, 0, 0, 0) != 0) {
					logError(list_path, ": ", lua_tostring(L, -1));
					return;
				}

				lua_getglobal(L, "resources");
				if (lua_type(L, -1) != LUA_TTABLE) return;

				{
					jobs::MutexGuard lock(m_resources_mutex);
					LuaWrapper::forEachArrayItem<Path>(L, -1, "array of strings expected", [this, &fs](const Path& p){
						const ResourceType type = getResourceType(p);
						#ifdef CACHE_MASTER 
							const Path res_path(".lumix/resources/", p.getHash(), ".res");
							if (type.isValid() && fs.fileExists(res_path)) {
								m_resources.insert(p.getHash(), {p, type, dirHash(p)});
							}
						#else
							if (type.isValid()) {
								if (fs.fileExists(ResourcePath::getResource(p))) {
									m_resources.insert(p.getHash(), {p, type, dirHash(p)});
								}
								else {
									const Path res_path(".lumix/resources/", p.getHash(), ".res");
									fs.deleteFile(res_path);
								}
							}
						#endif
					});
				}
				lua_pop(L, 1);

				lua_getglobal(L, "dependencies");
				if (lua_type(L, -1) != LUA_TTABLE) return;

				lua_pushnil(L);
				while (lua_next(L, -2) != 0) {
					if (!lua_isstring(L, -2) || !lua_istable(L, -1)) {
						logError("Invalid dependencies in _list.txt");
						lua_pop(L, 1);
						continue;
					}
					
					const char* key = lua_tostring(L, -2);
					const Path key_path(key);
					m_dependencies.insert(key_path, Array<Path>(m_allocator));
					Array<Path>& values = m_dependencies.find(key_path).value();

					LuaWrapper::forEachArrayItem<Path>(L, -1, "array of strings expected", [&values](const Path& p){ 
						values.push(p); 
					});

					lua_pop(L, 1);
				}
				lua_pop(L, 1);

			}();
		
			lua_close(L);
			for (IPlugin* plugin : m_plugins) {
				plugin->listLoaded();
			}
		}

		const u64 list_last_modified = os::getLastModified(list_path);
		processDir("", list_last_modified);
	}

	void onInitFinished() override
	{
		m_init_finished = true;
		for (Resource* res : m_on_init_load) {
			StringView filepath = ResourcePath::getResource(res->getPath());
			pushToCompileQueue(Path(filepath));
			res->decRefCount();
		}
		m_on_init_load.clear();
		fillDB();
	}

	void onFileChanged(const char* path)
	{
		if (startsWith(path, ".")) return;
		if (equalIStrings(path, "lumix.log")) return;

		const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
		const Path full_path(base_path, "/", path);

		if (os::dirExists(full_path)) {
			MutexGuard lock(m_changed_mutex);
			m_changed_dirs.push(Path(path));
		}
		else {
			MutexGuard lock(m_changed_mutex);
			m_changed_files.push(Path(path));
		}
	}

	lua_State* getMeta(const Path& res) override {
		const Path meta_path(res, ".meta");
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream buf(m_allocator);
		
		if (!fs.getContentSync(meta_path, buf)) return nullptr;

		lua_State* L = luaL_newstate();
		if (!LuaWrapper::execute(L, StringView((const char*)buf.data(), (u32)buf.size()), meta_path.c_str(), 0)) {
			lua_close(L);
			return nullptr;
		}

		return L;
	}

	void updateMeta(const Path& res, Span<const u8> data) const override {
		const Path meta_path(res, ".meta");
				
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.saveContentSync(meta_path, data)) {
			logError("Could not save ", meta_path);
		}
	}

	IPlugin* getPlugin(const Path& path) {
		StringView ext = Path::getExtension(path);
		char tmp[64];
		copyString(Span(tmp), ext);
		makeLowercase(Span(tmp), tmp);
		const RuntimeHash hash(tmp);
		MutexGuard lock(m_plugin_mutex);
		auto iter = m_plugins.find(hash);
		return iter.isValid() ? iter.value() : nullptr;
	}

	bool compile(const Path& src) override
	{
		IPlugin* plugin = getPlugin(src);
		if (!plugin) {
			logError("Unknown resource type ", src);
			return false;
		}
		return plugin->compile(src);
	}
	
	ResourceManagerHub::LoadHook::Action onBeforeLoad(Resource& res) {
		StringView filepath = ResourcePath::getResource(res.getPath());

		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.fileExists(filepath)) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;
		if (startsWith(filepath, ".lumix/resources/")) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;
		if (startsWith(filepath, ".lumix/asset_tiles/")) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;

		const FilePathHash hash = res.getPath().getHash();
		const Path dst_path(".lumix/resources/", hash, ".res");
		const Path meta_path(filepath, ".meta");

		if (!fs.fileExists(dst_path)
			|| fs.getLastModified(dst_path) < fs.getLastModified(filepath)
			|| fs.getLastModified(dst_path) < fs.getLastModified(meta_path)
			)
		{
			if (!m_init_finished) {
				res.incRefCount();
				m_on_init_load.push(&res);
				return ResourceManagerHub::LoadHook::Action::DEFERRED;
			}
			if (!getPlugin(res.getPath())) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;

			pushToCompileQueue(Path(filepath));
			return ResourceManagerHub::LoadHook::Action::DEFERRED;
		}
		return ResourceManagerHub::LoadHook::Action::IMMEDIATE;
	}

	void pushToCompileQueue(const Path& path) {
		auto iter = m_generations.find(path);
		if (!iter.isValid()) {
			iter = m_generations.insert(path, 0);
		}
		else {
			++iter.value();
		}

		CompileJob job;
		job.path = path;
		job.generation = iter.value();

		m_to_compile.push(job);
		++m_compile_batch_count;
		++m_batch_remaining_count;
	}

	CompileJob popCompiledResource()
	{
		MutexGuard lock(m_compiled_mutex);
		if (m_compiled.empty()) return {};
		const CompileJob p = m_compiled.back();
		m_compiled.pop();
		--m_batch_remaining_count;
		if (m_batch_remaining_count == 0) m_compile_batch_count = 0;
		return p;
	}

	void onGUI() override {
		if (m_batch_remaining_count == 0) return;
		const float ui_width = maximum(300.f, ImGui::GetIO().DisplaySize.x * 0.33f);

		const ImVec2 pos = ImGui::GetMainViewport()->Pos;
		ImGui::SetNextWindowPos(ImVec2((ImGui::GetIO().DisplaySize.x - ui_width) * 0.5f + pos.x, 30 + pos.y));
		ImGui::SetNextWindowSize(ImVec2(ui_width, -1));
		ImGui::SetNextWindowSizeConstraints(ImVec2(-FLT_MAX, 0), ImVec2(FLT_MAX, 200));
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoInputs
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
		if (ImGui::Begin("Resource compilation", nullptr, flags)) {
			ImGui::TextUnformatted("Compiling resources...");
			ImGui::ProgressBar(((float)m_compile_batch_count - m_batch_remaining_count) / m_compile_batch_count);
			ImGui::TextWrapped("%s", m_res_in_progress.c_str());
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}

	Resource* getResource(const Path& path) const {
		ResourceManagerHub& rman = m_app.getEngine().getResourceManager();
		for (ResourceManager* rm : rman.getAll()) {
			auto iter = rm->getResourceTable().find(path.getHash());
			if (iter.isValid()) return iter.value();
		}
		return nullptr;
	}

	void runOneJob() {
		if (m_to_compile.empty()) return;

		AssetCompilerImpl::CompileJob p = m_to_compile.back();
		m_to_compile.pop();
		auto iter = m_generations.find(p.path);
		const bool is_most_recent = p.generation == iter.value();
		if (!is_most_recent) {
			--m_batch_remaining_count;
			return;
		}

		m_res_in_progress = p.path.c_str();

		jobs::runLambda([p, this]() mutable {
			PROFILE_BLOCK("compile asset");
			profiler::pushString(p.path.c_str());
			p.compiled = compile(p.path);
			if (!p.compiled) logError("Failed to compile resource ", p.path);
			MutexGuard lock(m_compiled_mutex);
			m_compiled.push(p);
		}, nullptr);
	}

	void update() override {
		for(;;) {
			runOneJob();
			CompileJob job = popCompiledResource();
			if (job.path.isEmpty()) break;

			const u32 generation = m_generations[job.path];
			if (job.generation != generation) continue;

			// this can take some time, mutex is probably not the best option
			jobs::MutexGuard lock(m_resources_mutex);
			// reload/continue loading resource and its subresources
			for (const ResourceItem& ri : m_resources) {
				if (!endsWithInsensitive(ri.path, job.path)) continue;
				
				Resource* r = getResource(ri.path);
				if (r) {
					if (r->isReady() || r->isFailure()) r->getResourceManager().reload(*r);
					else if (r->isHooked()) m_load_hook.continueLoad(*r, job.compiled);
					m_resource_compiled.invoke(*r, job.compiled);
				}
			}

			// compile all dependents
			auto dep_iter = m_dependencies.find(job.path);
			if (dep_iter.isValid()) {
				for (const Path& p : dep_iter.value()) {
					pushToCompileQueue(p);
				}
			}
		}

		for (;;) {
			Path path_obj;
			{
				MutexGuard lock(m_changed_mutex);
				if (m_changed_dirs.empty()) break;

				m_changed_dirs.removeDuplicates();
				path_obj = m_changed_dirs.back();
				m_changed_dirs.pop();
			}

			if (!path_obj.isEmpty()) {
				FileSystem& fs = m_app.getEngine().getFileSystem();
				const Path list_path(fs.getBasePath(), ".lumix/resources/_list.txt");
				const u64 list_last_modified = os::getLastModified(list_path);
				const Path fullpath(fs.getBasePath(), path_obj);
				if (os::dirExists(fullpath)) {
					processDir(path_obj, list_last_modified);
					m_on_list_changed.invoke(path_obj);
				}
				else {
					jobs::MutexGuard lock(m_resources_mutex);
					m_resources.eraseIf([&](const ResourceItem& ri){
						if (!startsWith(ri.path, path_obj)) return false;
						return true;
					});
					m_on_list_changed.invoke(path_obj);
				}
			}
		}

		for (;;) {
			Path path_obj;
			{
				MutexGuard lock(m_changed_mutex);
				if (m_changed_files.empty()) break;

				m_changed_files.removeDuplicates();
				path_obj = m_changed_files.back();
				m_changed_files.pop();
			}

			if (Path::hasExtension(path_obj, "meta")) {
				StringView tmp = path_obj;
				tmp.removeSuffix(5);
				path_obj = tmp;
			}

			if (getResourceType(path_obj) != INVALID_RESOURCE_TYPE) {
				if (!m_app.getEngine().getFileSystem().fileExists(path_obj)) {
					jobs::MutexGuard lock(m_resources_mutex);
					m_resources.eraseIf([&](const ResourceItem& ri){
						if (!endsWithInsensitive(ri.path, path_obj)) return false;
						return true;
					});
					m_on_list_changed.invoke(path_obj);
				}
				else {
					addResource(path_obj);
					pushToCompileQueue(path_obj);
				}
			}
			else {
				auto dep_iter = m_dependencies.find(path_obj);
				if (dep_iter.isValid()) {
					for (const Path& p : dep_iter.value()) {
						pushToCompileQueue(p);
					}
				}
			}
		}
	}

	void removePlugin(IPlugin& plugin) override
	{
		MutexGuard lock(m_plugin_mutex);
		bool removed;
		do {
			removed = false;
			for(auto iter = m_plugins.begin(), end = m_plugins.end(); iter != end; ++iter) {
				if (iter.value() == &plugin) {
					m_plugins.erase(iter);
					removed = true;
					break;
				}
			}
		} while(removed);
	}

	void addPlugin(IPlugin& plugin, Span<const char*> extensions) override {
		for (const char* ext : extensions) {
			const RuntimeHash hash(ext);
			MutexGuard lock(m_plugin_mutex);
			m_plugins.insert(hash, &plugin);
		}
	}

	void unlockResources() override {
		jobs::exit(&m_resources_mutex);
	}

	const HashMap<FilePathHash, ResourceItem>& lockResources() override {
		jobs::enter(&m_resources_mutex);
		return m_resources;
	}

	TagAllocator m_allocator;
	Mutex m_compiled_mutex;
	Mutex m_changed_mutex;
	Mutex m_plugin_mutex;
	jobs::Mutex m_resources_mutex;
	HashMap<Path, u32> m_generations; 
	HashMap<Path, Array<Path>> m_dependencies; 
	Array<Path> m_changed_files;
	Array<Path> m_changed_dirs;
	Array<CompileJob> m_to_compile;
	Array<CompileJob> m_compiled;
	StudioApp& m_app;
	LoadHook m_load_hook;
	HashMap<RuntimeHash, IPlugin*> m_plugins;
	UniquePtr<FileSystemWatcher> m_watcher;
	HashMap<FilePathHash, ResourceItem> m_resources;
	HashMap<u32, ResourceType, HashFuncDirect<u32>> m_registered_extensions;
	DelegateList<void(const Path&)> m_on_list_changed;
	DelegateList<void(Resource&, bool)> m_resource_compiled;
	bool m_init_finished = false;
	Array<Resource*> m_on_init_load;
	u8* m_lz4_state = nullptr;
	jobs::Mutex m_lz4_mutex;

	u32 m_compile_batch_count = 0;
	u32 m_batch_remaining_count = 0;
	Path m_res_in_progress;
};


UniquePtr<AssetCompiler> AssetCompiler::create(StudioApp& app) {
	return UniquePtr<AssetCompilerImpl>::create(app.getAllocator(), app);
}


} // namespace Lumix

