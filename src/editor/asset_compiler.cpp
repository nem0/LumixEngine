#include <imgui/imgui.h>

#include "asset_compiler.h"
#include "editor/file_system_watcher.h"
#include "editor/log_ui.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/atomic.h"
#include "engine/engine.h"
#include "engine/hash.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/atomic.h"
#include "engine/sync.h"
#include "engine/thread.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "lz4/lz4.h"

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


struct AssetCompilerTask : Thread
{
	AssetCompilerTask(AssetCompilerImpl& compiler, IAllocator& allocator) 
		: Thread(allocator) 
		 , m_compiler(compiler)
	{}

	int task() override;

	AssetCompilerImpl& m_compiler;
	volatile bool m_finished = false;
};


void AssetCompiler::IPlugin::addSubresources(AssetCompiler& compiler, const char* path)
{
	const ResourceType type = compiler.getResourceType(path);
	if (!type.isValid()) return;
	
	compiler.addResource(type, path);
}


struct AssetCompilerImpl : AssetCompiler {
	struct CompileJob {
		u32 generation;
		Path path;
	};

	struct LoadHook : ResourceManagerHub::LoadHook
	{
		LoadHook(AssetCompilerImpl& compiler) : compiler(compiler) {}

		Action onBeforeLoad(Resource& res) override
		{
			return compiler.onBeforeLoad(res);
		}

		AssetCompilerImpl& compiler;
	};


	AssetCompilerImpl(StudioApp& app) 
		: m_app(app)
		, m_load_hook(*this)
		, m_plugins(app.getAllocator())
		, m_task(*this, app.getAllocator())
		, m_to_compile(app.getAllocator())
		, m_compiled(app.getAllocator())
		, m_semaphore(0, 0x7fFFffFF)
		, m_registered_extensions(app.getAllocator())
		, m_resources(app.getAllocator())
		, m_generations(app.getAllocator())
		, m_dependencies(app.getAllocator())
		, m_changed_files(app.getAllocator())
		, m_changed_dirs(app.getAllocator())
		, m_on_list_changed(app.getAllocator())
		, m_on_init_load(app.getAllocator())
	{
		Engine& engine = app.getEngine();
		FileSystem& fs = engine.getFileSystem();
		const char* base_path = fs.getBasePath();
		m_watcher = FileSystemWatcher::create(base_path, app.getAllocator());
		m_watcher->getCallback().bind<&AssetCompilerImpl::onFileChanged>(this);
		m_task.create("Asset compiler", true);
		StaticString<LUMIX_MAX_PATH> path(base_path, ".lumix/resources");
		if (!os::dirExists(path)) {
			if (!os::makePath(path)) logError("Could not create ", path);
			else {
				os::OutputFile file;
				path.add("/_version.bin");
				if (!file.open(path)) {
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
				os::FileIterator* iter = os::createFileIterator(".lumix/resources", m_app.getAllocator());
				os::FileInfo info;
				bool all_deleted = true;
				while (os::getNextFile(iter, &info)) {
					if (!info.is_directory) {
						StaticString<LUMIX_MAX_PATH> path(".lumix/resources/", info.filename);
						if (!os::deleteFile(path)) {
							all_deleted = false;
						}
					}
				}
				os::destroyFileIterator(iter);

				if (!all_deleted) {
					logError("Could not delete all files in .lumix/resources, please delete the directory and restart the editor.");
				}

				os::OutputFile file;
				if (!file.open(".lumix/resources/_version.bin")) {
					logError("Could not open .lumix/resources/_version.bin");
				}
				else {
					file.write(0);
					file.close();
				}
			}
		}

		ResourceManagerHub& rm = engine.getResourceManager();
		rm.setLoadHook(&m_load_hook);
	}

	~AssetCompilerImpl()
	{
		os::OutputFile file;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (fs.open(".lumix/resources/_list.txt_tmp", file)) {
			file << "resources = {\n";
			for (const ResourceItem& ri : m_resources) {
				file << "\"" << ri.path.c_str() << "\",\n";
			}
			file << "}\n\n";
			file << "dependencies = {\n";
			for (auto iter = m_dependencies.begin(), end = m_dependencies.end(); iter != end; ++iter) {
				file << "\t[\"" << iter.key().c_str() << "\"] = {\n";
				for (const Path& p : iter.value()) {
					file << "\t\t\"" << p.c_str() << "\",\n";
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
		m_task.m_finished = true;
		m_to_compile.emplace();
		m_semaphore.signal();
		m_task.destroy();
		ResourceManagerHub& rm = m_app.getEngine().getResourceManager();
		rm.setLoadHook(nullptr);
	}
	
	void onBasePathChanged() override {
		Engine& engine = m_app.getEngine();
		FileSystem& fs = engine.getFileSystem();
		const char* base_path = fs.getBasePath();
		m_watcher = FileSystemWatcher::create(base_path, m_app.getAllocator());
		m_watcher->getCallback().bind<&AssetCompilerImpl::onFileChanged>(this);
		m_dependencies.clear();
		m_resources.clear();
		fillDB();
	}

	DelegateList<void(const Path&)>& listChanged() override {
		return m_on_list_changed;
	}

	bool copyCompile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream tmp(m_app.getAllocator());
		if (!fs.getContentSync(src, tmp)) {
			logError("Failed to read ", src);
			return false;
		}

		ASSERT(tmp.size() < 0xffFFffFF);
		return writeCompiledResource(src.c_str(), Span(tmp.data(), (u32)tmp.size()));
	}

	bool writeCompiledResource(const char* locator, Span<const u8> data) override {
		constexpr u32 COMPRESSION_SIZE_LIMIT = 4096;
		OutputMemoryStream compressed(m_app.getAllocator());
		i32 compressed_size = 0;
		if (data.length() > COMPRESSION_SIZE_LIMIT) {
			const i32 cap = LZ4_compressBound((i32)data.length());
			compressed.resize(cap);
			compressed_size = LZ4_compress_default((const char*)data.begin(), (char*)compressed.getMutableData(), (i32)data.length(), cap); 
			if (compressed_size == 0) {
				logError("Could not compress ", locator);
				return false;
			}
			compressed.resize(compressed_size);
		}

		char normalized[LUMIX_MAX_PATH];
		Path::normalize(locator, Span(normalized));
		makeLowercase(Span(normalized), normalized);
		const FilePathHash hash(normalized);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		StaticString<LUMIX_MAX_PATH> out_path(".lumix/resources/", hash, ".res");
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

	static RuntimeHash dirHash(const char* path) {
		char tmp[LUMIX_MAX_PATH];
		copyString(Span(tmp), Path::getDir(getResourceFilePath(path)));
		makeLowercase(Span(tmp), tmp);
		Span<const char> dir(tmp, stringLength(tmp));
		if (dir.m_end > dir.m_begin && (*(dir.m_end - 1) == '\\' || *(dir.m_end - 1) == '/')) {
			--dir.m_end;
		} 
		return RuntimeHash(dir.begin(), dir.length());
	}

	void addResource(ResourceType type, const char* path) override {
		const Path path_obj(path);
		const FilePathHash hash = path_obj.getHash();
		jobs::MutexGuard lock(m_resources_mutex);
		if (m_resources.find(hash).isValid()) {
			m_resources[hash] = {path_obj, type, dirHash(path_obj.c_str())};
		}
		else {
			m_resources.insert(hash, {path_obj, type, dirHash(path_obj.c_str())});
			m_on_list_changed.invoke(path_obj);
		}
	}

	ResourceType getResourceType(const char* path) const override
	{
		const Span<const char> subres = getSubresource(path);
		Span<const char> ext = Path::getExtension(subres);

		alignas(u32) char tmp[6] = {};
		makeLowercase(Span(tmp), ext);
		ASSERT(strlen(tmp) < 5);
		auto iter = m_registered_extensions.find(*(u32*)tmp); //-V641 
		if (iter.isValid()) return iter.value();

		return INVALID_RESOURCE_TYPE;
	}


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		alignas(u32) char tmp[6] = {};
		makeLowercase(Span(tmp), ext);
		ASSERT(strlen(tmp) < 5);
		auto iter = m_registered_extensions.find(*(u32*)tmp); //-V641 
		if (!iter.isValid()) return false;
		return iter.value() == type;
	}

	
	void registerExtension(const char* extension, ResourceType type) override
	{
		alignas(u32) char tmp[6] = {};
		makeLowercase(Span(tmp), extension);
		ASSERT(strlen(tmp) < 5);
		u32 q = *(u32*)tmp; //-V641 
		ASSERT(!m_registered_extensions.find(q).isValid());

		m_registered_extensions.insert(q, type);
	}


	void addResource(const char* fullpath)
	{
		char ext[10];
		copyString(Span(ext), Path::getExtension(Span(fullpath, stringLength(fullpath))));
		makeLowercase(Span(ext), ext);
	
		auto iter = m_plugins.find(RuntimeHash(ext));
		if (!iter.isValid()) return;

		iter.value()->addSubresources(*this, fullpath);
	}

	
	void processDir(const char* dir, u64 list_last_modified)
	{
		FileSystem& fs = m_app.getEngine().getFileSystem();
		auto* iter = fs.createFileIterator(dir);
		os::FileInfo info;
		while (getNextFile(iter, &info))
		{
			if (info.filename[0] == '.') continue;

			if (info.is_directory)
			{
				char child_path[LUMIX_MAX_PATH];
				copyString(child_path, dir);
				if(dir[0]) catString(child_path, "/");
				catString(child_path, info.filename);
				processDir(child_path, list_last_modified);
			}
			else
			{
				char fullpath[LUMIX_MAX_PATH];
				copyString(fullpath, dir);
				if(dir[0]) catString(fullpath, "/");
				catString(fullpath, info.filename);

				if (fs.getLastModified(fullpath[0] == '/' ? fullpath + 1 : fullpath) > list_last_modified) {
					addResource(fullpath);
				}
				else {
					Path path(fullpath[0] == '/' ? fullpath + 1 : fullpath);
					if (!m_resources.find(path.getHash()).isValid()) {
						addResource(fullpath);
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
			IAllocator& allocator = m_app.getAllocator();
			m_dependencies.insert(dependency, Array<Path>(allocator));
			iter = m_dependencies.find(dependency);
		}
		if (iter.value().indexOf(included_from) < 0) {
			iter.value().push(included_from);
		}
	}

	void fillDB() {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		const StaticString<LUMIX_MAX_PATH> list_path(fs.getBasePath(), ".lumix/resources/_list.txt");
		OutputMemoryStream content(m_app.getAllocator());
		if (fs.getContentSync(Path(".lumix/resources/_list.txt"), content)) {
			lua_State* L = luaL_newstate();
			[&](){
				if (luaL_loadbuffer(L, (const char*)content.data(), content.size(), "lumix_asset_list") != 0) {
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
						const ResourceType type = getResourceType(p.c_str());
						#ifdef CACHE_MASTER 
							StaticString<LUMIX_MAX_PATH> res_path(".lumix/resources/", p.getHash(), ".res");
							if (type.isValid() && fs.fileExists(res_path)) {
								m_resources.insert(p.getHash(), {p, type, dirHash(p.c_str())});
							}
						#else
							if (type.isValid()) {
								ResourceLocator locator(Span<const char>(p.c_str(), (u32)strlen(p.c_str())));
								char tmp[LUMIX_MAX_PATH];
								copyString(Span(tmp), locator.resource);
								if (fs.fileExists(tmp)) {
									m_resources.insert(p.getHash(), {p, type, dirHash(p.c_str())});
								}
								else {
									StaticString<LUMIX_MAX_PATH> res_path(".lumix/resources/", p.getHash(), ".res");
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
					IAllocator& allocator = m_app.getAllocator();
					const Path key_path(key);
					m_dependencies.insert(key_path, Array<Path>(allocator));
					Array<Path>& values = m_dependencies.find(key_path).value();

					LuaWrapper::forEachArrayItem<Path>(L, -1, "array of strings expected", [&values](const Path& p){ 
						values.push(p); 
					});

					lua_pop(L, 1);
				}
				lua_pop(L, 1);

			}();
		
			lua_close(L);
		}

		const u64 list_last_modified = os::getLastModified(list_path);
		processDir("", list_last_modified);
	}

	void onInitFinished() override
	{
		m_init_finished = true;
		for (Resource* res : m_on_init_load) {
			const char* filepath = getResourceFilePath(res->getPath().c_str());
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
		const StaticString<LUMIX_MAX_PATH> full_path(base_path, "/", path);

		if (os::dirExists(full_path)) {
			MutexGuard lock(m_changed_mutex);
			m_changed_dirs.push(Path(path));
		}
		else {
			MutexGuard lock(m_changed_mutex);
			m_changed_files.push(Path(path));
		}
	}

	bool getMeta(const Path& res, void* user_ptr, void (*callback)(void*, lua_State*)) const override
	{
		const StaticString<LUMIX_MAX_PATH> meta_path(res.c_str(), ".meta");
		
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream buf(m_app.getAllocator());
		
		if (!fs.getContentSync(Path(meta_path), buf)) return false;

		lua_State* L = luaL_newstate();
		if (luaL_loadbuffer(L, (const char*)buf.data(), buf.size(), meta_path) != 0) {
			logError(meta_path, ": ", lua_tostring(L, -1));
			lua_close(L);
			return false;
		}

		if (lua_pcall(L, 0, 0, 0) != 0) {
			logError(meta_path, ": ", lua_tostring(L, -1));
			lua_close(L);
			return false;
		}

		callback(user_ptr, L);

		lua_close(L);
		return true;
	}


	void updateMeta(const Path& res, const char* src) const override
	{
		os::OutputFile file;
		const StaticString<LUMIX_MAX_PATH> meta_path(res.c_str(), ".meta");
				
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.open(meta_path, file)) {
			logError("Could not create ", meta_path);
			return;
		}

		if (!file.write(src, stringLength(src))) {
			logError("Could not write ", meta_path);
		}
		file.close();
	}


	bool compile(const Path& src) override
	{
		Span<const char> ext = Path::getExtension(Span(src.c_str(), src.length()));
		char tmp[64];
		copyString(Span(tmp), ext);
		makeLowercase(Span(tmp), tmp);
		const RuntimeHash hash(tmp);
		MutexGuard lock(m_plugin_mutex);
		auto iter = m_plugins.find(hash);
		if (!iter.isValid()) {
			logError("Unknown resource type ", src);
			return false;
		}
		return iter.value()->compile(src);
	}
	

	static const char* getResourceFilePath(const char* str)
	{
		const char* c = str;
		while (*c && *c != ':') ++c;
		return *c != ':' ? str : c + 1;
	}

	static Span<const char> getSubresource(const char* str)
	{
		Span<const char> ret;
		ret.m_begin = str;
		ret.m_end = str;
		while(*ret.m_end && *ret.m_end != ':') ++ret.m_end;
		return ret;
	}

	ResourceManagerHub::LoadHook::Action onBeforeLoad(Resource& res)
	{
		const char* filepath = getResourceFilePath(res.getPath().c_str());

		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.fileExists(filepath)) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;
		if (startsWith(filepath, ".lumix/resources/")) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;
		if (startsWith(filepath, ".lumix/asset_tiles/")) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;

		const FilePathHash hash = res.getPath().getHash();
		const StaticString<LUMIX_MAX_PATH> dst_path(".lumix/resources/", hash, ".res");
		const StaticString<LUMIX_MAX_PATH> meta_path(filepath, ".meta");

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

			pushToCompileQueue(Path(filepath));
			return ResourceManagerHub::LoadHook::Action::DEFERRED;
		}
		return ResourceManagerHub::LoadHook::Action::IMMEDIATE;
	}

	void pushToCompileQueue(const Path& path) {
		MutexGuard lock(m_to_compile_mutex);
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
		m_semaphore.signal();
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
			ImGui::Text("%s", "Compiling resources...");
			ImGui::ProgressBar(((float)m_compile_batch_count - m_batch_remaining_count) / m_compile_batch_count);
			StaticString<LUMIX_MAX_PATH> path;
			{
				MutexGuard lock(m_to_compile_mutex);
				path = m_res_in_progress;
			}
			ImGui::TextWrapped("%s", path.data);
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

	void update() override
	{
		for(;;) {
			CompileJob p = popCompiledResource();
			if (p.path.isEmpty()) break;

			// this can take some time, mutex is probably not the best option

			const u32 generation = [&](){
				MutexGuard lock(m_to_compile_mutex);
				return m_generations[p.path];
			}();
			if (p.generation != generation) continue;

			MutexGuard lock(m_compiled_mutex);
			// reload/continue loading resource and its subresources
			for (const ResourceItem& ri : m_resources) {
				if (!endsWithInsensitive(ri.path.c_str(), p.path.c_str())) continue;;
				
				Resource* r = getResource(ri.path);
				if (r && (r->isReady() || r->isFailure())) r->getResourceManager().reload(*r);
				else if (r && r->isHooked()) m_load_hook.continueLoad(*r);
			}

			// compile all dependents
			auto dep_iter = m_dependencies.find(p.path);
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
				const StaticString<LUMIX_MAX_PATH> list_path(fs.getBasePath(), ".lumix/resources/_list.txt");
				const u64 list_last_modified = os::getLastModified(list_path);
				StaticString<LUMIX_MAX_PATH> fullpath(fs.getBasePath(), path_obj.c_str());
				if (os::dirExists(fullpath)) {
					processDir(path_obj.c_str(), list_last_modified);
					m_on_list_changed.invoke(path_obj);
				}
				else {
					jobs::MutexGuard lock(m_resources_mutex);
					m_resources.eraseIf([&](const ResourceItem& ri){
						if (!startsWith(ri.path.c_str(), path_obj.c_str())) return false;
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

			if (Path::hasExtension(path_obj.c_str(), "meta")) {
				char tmp[LUMIX_MAX_PATH];
				copyNString(Span(tmp), path_obj.c_str(), path_obj.length() - 5);
				path_obj = tmp;
			}

			if (getResourceType(path_obj.c_str()) != INVALID_RESOURCE_TYPE) {
				if (!m_app.getEngine().getFileSystem().fileExists(path_obj.c_str())) {
					jobs::MutexGuard lock(m_resources_mutex);
					m_resources.eraseIf([&](const ResourceItem& ri){
						if (!endsWithInsensitive(ri.path.c_str(), path_obj.c_str())) return false;
						return true;
					});
					m_on_list_changed.invoke(path_obj);
				}
				else {
					addResource(path_obj.c_str());
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

	void addPlugin(IPlugin& plugin, const char** extensions) override
	{
		const char** i = extensions;
		while(*i) {
			const RuntimeHash hash(*i);
			MutexGuard lock(m_plugin_mutex);
			m_plugins.insert(hash, &plugin);
			++i;
		}
	}

	void unlockResources() override {
		jobs::exit(&m_resources_mutex);
	}

	const HashMap<FilePathHash, ResourceItem>& lockResources() override {
		jobs::enter(&m_resources_mutex);
		return m_resources;
	}

	Semaphore m_semaphore;
	Mutex m_to_compile_mutex;
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
	AssetCompilerTask m_task;
	UniquePtr<FileSystemWatcher> m_watcher;
	HashMap<FilePathHash, ResourceItem> m_resources;
	HashMap<u32, ResourceType, HashFuncDirect<u32>> m_registered_extensions;
	DelegateList<void(const Path& path)> m_on_list_changed;
	bool m_init_finished = false;
	Array<Resource*> m_on_init_load;

	u32 m_compile_batch_count = 0;
	u32 m_batch_remaining_count = 0;
	StaticString<LUMIX_MAX_PATH> m_res_in_progress;
};


int AssetCompilerTask::task()
{
	while (!m_finished) {
		m_compiler.m_semaphore.wait();
		const AssetCompilerImpl::CompileJob p = [&]{
			MutexGuard lock(m_compiler.m_to_compile_mutex);
			AssetCompilerImpl::CompileJob p = m_compiler.m_to_compile.back();
			if (p.path.isEmpty()) return p;

			auto iter = m_compiler.m_generations.find(p.path);
			const bool is_most_recent = p.generation == iter.value();
			if (is_most_recent) {
				m_compiler.m_res_in_progress = p.path.c_str();
			}
			else {
				p.path = Path();
				--m_compiler.m_batch_remaining_count;
			}
			m_compiler.m_to_compile.pop();
			return p;
		}();
		if (!p.path.isEmpty()) {
			PROFILE_BLOCK("compile asset");
			profiler::pushString(p.path.c_str());
			const bool compiled = m_compiler.compile(p.path);
			if (!compiled) logError("Failed to compile resource ", p.path);
			MutexGuard lock(m_compiler.m_compiled_mutex);
			m_compiler.m_compiled.push(p);
		}
	}
	return 0;
}


UniquePtr<AssetCompiler> AssetCompiler::create(StudioApp& app) {
	return UniquePtr<AssetCompilerImpl>::create(app.getAllocator(), app);
}


} // namespace Lumix

