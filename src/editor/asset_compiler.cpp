#include <imgui/imgui.h>

#include "asset_compiler.h"
#include "editor/file_system_watcher.h"
#include "editor/log_ui.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
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


namespace Lumix
{


struct AssetCompilerImpl;


template<>
struct HashFunc<Path>
{
	static u32 get(const Path& key)
	{
		return key.getHash();
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
	if (type == INVALID_RESOURCE_TYPE) return;
	
	compiler.addResource(type, path);
}


struct AssetCompilerImpl : AssetCompiler
{
	struct CompileEntry
	{
		Path path;
		Resource* resource;
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
		, m_to_compile_subresources(app.getAllocator())
		, m_dependencies(app.getAllocator())
		, m_changed_files(app.getAllocator())
		, m_on_list_changed(app.getAllocator())
	{
		FileSystem& fs = app.getEngine().getFileSystem();
		m_watcher = FileSystemWatcher::create(fs.getBasePath(), app.getAllocator());
		m_watcher->getCallback().bind<&AssetCompilerImpl::onFileChanged>(this);
		m_task.create("Asset compiler", true);
		const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
		StaticString<MAX_PATH_LENGTH> path(base_path, ".lumix/assets");
		OS::makePath(path);
		ResourceManagerHub& rm = app.getEngine().getResourceManager();
		rm.setLoadHook(&m_load_hook);
	}

	~AssetCompilerImpl()
	{
		OS::OutputFile file;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (fs.open(".lumix/assets/_list.txt_tmp", Ref(file))) {
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
			fs.deleteFile(".lumix/assets/_list.txt");
			fs.moveFile(".lumix/assets/_list.txt_tmp", ".lumix/assets/_list.txt");
		}
		else {
			logError("Editor") << "Could not save .lumix/assets/_list.txt";
		}

		ASSERT(m_plugins.empty());
		m_task.m_finished = true;
		m_to_compile.emplace();
		m_semaphore.signal();
		m_task.destroy();
		ResourceManagerHub& rm = m_app.getEngine().getResourceManager();
		rm.setLoadHook(nullptr);
		FileSystemWatcher::destroy(m_watcher);
	}
	
	DelegateList<void()>& listChanged() override {
		return m_on_list_changed;
	}

	bool copyCompile(const Path& src) override {
		const StaticString<MAX_PATH_LENGTH> dst(".lumix/assets/", src.getHash(), ".res");

		FileSystem& fs = m_app.getEngine().getFileSystem();
		return fs.copyFile(src.c_str(), dst);
	}

	bool writeCompiledResource(const char* locator, Span<const u8> data) override {
		char normalized[MAX_PATH_LENGTH];
		Path::normalize(locator, Span(normalized));
		const u32 hash = crc32(normalized);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		StaticString<MAX_PATH_LENGTH> out_path(".lumix/assets/", hash, ".res");
		OS::OutputFile file;
		if(!fs.open(out_path, Ref(file))) {
			logError("Editor") << "Could not create " << out_path;
			return false;
		}
		const bool written = file.write(data.begin(), data.length());
		if (!written) logError("Editor") << "Could not write " << out_path;
		file.close();
		return written;
	}

	static u32 dirHash(const char* path) {
		char dir[MAX_PATH_LENGTH];
		Path::getDir(Span(dir), getResourceFilePath(path));
		int len = stringLength(dir);
		if (len > 0 && (dir[len - 1] == '/' || dir[len - 1] == '\\')) {
			--len;
		}
		return crc32(dir, len);
	}

	void addResource(ResourceType type, const char* path) override {
		const Path path_obj(path);
		const u32 hash = path_obj.getHash();
		MutexGuard lock(m_resources_mutex);
		if(m_resources.find(hash).isValid()) {
			m_resources[hash] = {path_obj, type};
		}
		else {
			m_resources.insert(hash, {path_obj, type, dirHash(path_obj.c_str())});
		}
	}

	ResourceType getResourceType(const char* path) const override
	{
		const Span<const char> subres = getSubresource(path);
		char ext[16];
		Path::getExtension(Span(ext), subres);

		auto iter = m_registered_extensions.find(crc32(ext));
		if (iter.isValid()) return iter.value();

		return INVALID_RESOURCE_TYPE;
	}


	bool acceptExtension(const char* ext, ResourceType type) const override
	{
		auto iter = m_registered_extensions.find(crc32(ext));
		if (!iter.isValid()) return false;
		return iter.value() == type;
	}

	
	void registerExtension(const char* extension, ResourceType type) override
	{
		const u32 hash = crc32(extension);
		ASSERT(!m_registered_extensions.find(hash).isValid());

		m_registered_extensions.insert(hash, type);
	}


	void addResource(const char* fullpath)
	{
		char ext[10];
		Path::getExtension(Span(ext), Span(fullpath, stringLength(fullpath)));
		makeLowercase(Span(ext), ext);
	
		auto iter = m_plugins.find(crc32(ext));
		if (!iter.isValid()) return;

		iter.value()->addSubresources(*this, fullpath);
	}

	
	void processDir(const char* dir, u64 list_last_modified)
	{
		FileSystem& fs = m_app.getEngine().getFileSystem();
		auto* iter = fs.createFileIterator(dir);
		OS::FileInfo info;
		while (getNextFile(iter, &info))
		{
			if (info.filename[0] == '.') continue;

			if (info.is_directory)
			{
				char child_path[MAX_PATH_LENGTH];
				copyString(child_path, dir);
				if(dir[0]) catString(child_path, "/");
				catString(child_path, info.filename);
				processDir(child_path, list_last_modified);
			}
			else
			{
				char fullpath[MAX_PATH_LENGTH];
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
		iter.value().push(included_from);
	}


	void onInitFinished() override
	{
		OS::InputFile file;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		const StaticString<MAX_PATH_LENGTH> list_path(fs.getBasePath(), ".lumix/assets/_list.txt");
		if (fs.open(".lumix/assets/_list.txt", Ref(file))) {
			Array<char> content(m_app.getAllocator());
			content.resize((int)file.size());
			file.read(content.begin(), content.byte_size());
			file.close();

			lua_State* L = luaL_newstate();
			[&](){
				if (luaL_loadbuffer(L, content.begin(), content.byte_size(), "lumix_asset_list") != 0) {
					logError("Editor") << list_path << ": " << lua_tostring(L, -1);
					return;
				}

				if (lua_pcall(L, 0, 0, 0) != 0) {
					logError("Editor") << list_path << ": " << lua_tostring(L, -1);
					return;
				}

				lua_getglobal(L, "resources");
				if (lua_type(L, -1) != LUA_TTABLE) return;

				{
					MutexGuard lock(m_resources_mutex);
					LuaWrapper::forEachArrayItem<Path>(L, -1, "array of strings expected", [this, &fs](const Path& p){
						const ResourceType type = getResourceType(p.c_str());
						StaticString<MAX_PATH_LENGTH> res_path(".lumix/assets/", p.getHash(), ".res");
						#if 0 // use this if you want to be able to use cached resources without having the original
							if (type != INVALID_RESOURCE_TYPE && fs.fileExists(res_path)) {
								m_resources.insert(p.getHash(), {p, type, dirHash(p.c_str())});
							}
						#else
							if (type != INVALID_RESOURCE_TYPE && fs.fileExists(p.c_str())) {
								m_resources.insert(p.getHash(), {p, type, dirHash(p.c_str())});
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
						logError("Editor") << "Invalid dependencies in _list.txt";
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

		const u64 list_last_modified = OS::getLastModified(list_path);
		processDir("", list_last_modified);

		registerLuaAPI(m_app.getEngine().getState());
	}


	Array<Path> removeResource(const char* path)
	{
		Array<Path> res(m_app.getAllocator());

		MutexGuard lock(m_resources_mutex);
		m_resources.eraseIf([&](const ResourceItem& ri){
			if (!equalStrings(getResourceFilePath(ri.path.c_str()), path)) return false;
			res.push(ri.path);
			return true;
		});

		const Path path_obj(path);
		for (Array<Path>& deps : m_dependencies) {
			deps.eraseItems([&](const Path& p){ return p == path_obj; });
		}

		return res;
	}


	void reloadSubresources(const Array<Path>& subresources)
	{
		ResourceManagerHub& rman = m_app.getEngine().getResourceManager();
		for (const Path& p : subresources) {
			rman.reload(p);
		}
	}


	void onFileChanged(const char* path)
	{
		if (startsWith(path, ".lumix")) return;
		if (equalIStrings(path, "lumix.log")) return;
		
		MutexGuard lock(m_changed_mutex);
		m_changed_files.push(Path(path));
	}

	bool getMeta(const Path& res, void* user_ptr, void (*callback)(void*, lua_State*)) const override
	{
		OS::InputFile file;
		const StaticString<MAX_PATH_LENGTH> meta_path(res.c_str(), ".meta");
		
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.open(meta_path, Ref(file))) return false;

		Array<char> buf(m_app.getAllocator());
		buf.resize((int)file.size());
		const bool read_all = file.read(buf.begin(), buf.byte_size());
		file.close();
		if (!read_all) {
			return false;
		}

		lua_State* L = luaL_newstate();
		if (luaL_loadbuffer(L, buf.begin(), buf.byte_size(), meta_path) != 0) {
			logError("Editor") << meta_path << ": " << lua_tostring(L, -1);
			lua_close(L);
			return false;
		}

		if (lua_pcall(L, 0, 0, 0) != 0) {
			logError("Engine") << meta_path << ": " << lua_tostring(L, -1);
			lua_close(L);
			return false;
		}

		callback(user_ptr, L);

		lua_close(L);
		return true;
	}


	void updateMeta(const Path& res, const char* src) const override
	{
		OS::OutputFile file;
		const StaticString<MAX_PATH_LENGTH> meta_path(res.c_str(), ".meta");
				
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.open(meta_path, Ref(file))) {
			logError("Editor") << "Could not create " << meta_path;
			return;
		}

		file.write(src, stringLength(src));
		file.close();
	}


	bool compile(const Path& src) override
	{
		char ext[16];
		Path::getExtension(Span(ext), Span(src.c_str(), src.length()));
		const u32 hash = crc32(ext);
		MutexGuard lock(m_plugin_mutex);
		auto iter = m_plugins.find(hash);
		if (!iter.isValid()) {
			logError("Editor") << "Unknown resource type " << src;
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
		if (startsWith(filepath, ".lumix/assets/")) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;

		const u32 hash = res.getPath().getHash();
		const StaticString<MAX_PATH_LENGTH> dst_path(".lumix/assets/", hash, ".res");
		const StaticString<MAX_PATH_LENGTH> meta_path(filepath, ".meta");

		if (!fs.fileExists(dst_path)
			|| fs.getLastModified(dst_path) < fs.getLastModified(filepath)
			|| fs.getLastModified(dst_path) < fs.getLastModified(meta_path)
			)
		{
			logInfo("Editor") << res.getPath() << " is not compiled, pushing to compile queue";
			MutexGuard lock(m_to_compile_mutex);
			const Path path(filepath);
			auto iter = m_to_compile_subresources.find(path);
			if (!iter.isValid()) {
				m_to_compile.push(path);
				++m_compile_batch_count;
				++m_batch_remaining_count;
				m_semaphore.signal();
				IAllocator& allocator = m_app.getAllocator();
				m_to_compile_subresources.insert(path, Array<Resource*>(allocator));
				iter = m_to_compile_subresources.find(path);
			}
			iter.value().push(&res);
			return ResourceManagerHub::LoadHook::Action::DEFERRED;
		}
		return ResourceManagerHub::LoadHook::Action::IMMEDIATE;
	}

	Path popCompiledResource()
	{
		MutexGuard lock(m_compiled_mutex);
		if(m_compiled.empty()) return Path();
		const Path p = m_compiled.back();
		m_compiled.pop();
		--m_batch_remaining_count;
		if (m_batch_remaining_count == 0) m_compile_batch_count = 0;
		return p;
	}
	
	static int LUA_getResources(lua_State* L) {
		const int index = lua_upvalueindex(1);
		if (!LuaWrapper::isType<AssetCompilerImpl*>(L, index)) {
			logError("Lua") << "Invalid Lua closure";
			ASSERT(false);
			return 0;
		}
		AssetCompilerImpl* compiler = LuaWrapper::toType<AssetCompilerImpl*>(L, index);
		ASSERT(compiler);

		MutexGuard lock(compiler->m_resources_mutex);
		lua_createtable(L, 0, compiler->m_resources.size());
		for (ResourceItem& ri : compiler->m_resources) {
			lua_pushinteger(L, ri.type.type);
			lua_setfield(L, -2, ri.path.c_str());
		}
		return 1;
	}

	void registerLuaAPI(lua_State* L) {
		LuaWrapper::createSystemClosure(L, "Assets", this, "getResources", &LUA_getResources);
	}

	void onGUI() override {
		if (m_batch_remaining_count == 0) return;
		const float ui_width = maximum(300.f, ImGui::GetIO().DisplaySize.x * 0.33f);

		const ImVec2 pos = ImGui::GetMainViewport()->Pos;
		ImGui::SetNextWindowPos(ImVec2((ImGui::GetIO().DisplaySize.x - ui_width) * 0.5f + pos.x, 30 + pos.y));
		ImGui::SetNextWindowSize(ImVec2(ui_width, -1));
		ImGui::SetNextWindowSizeConstraints(ImVec2(-FLT_MAX, 0), ImVec2(FLT_MAX, 200));
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar 
			| ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
		if (ImGui::Begin("Resource compilation", nullptr, flags)) {
			ImGui::Text("%s", "Compiling resources...");
			ImGui::ProgressBar(((float)m_compile_batch_count - m_batch_remaining_count) / m_compile_batch_count);
			StaticString<MAX_PATH_LENGTH> path;
			{
				MutexGuard lock(m_to_compile_mutex);
				path = m_res_in_progress;
			}
			ImGui::TextWrapped("%s", path.data);
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}

	void update() override
	{
		for(;;) {
			Path p = popCompiledResource();
			if (!p.isValid()) break;

			// this can take some time, mutex is probably not the best option
			MutexGuard lock(m_compiled_mutex);

			for (Resource* r : m_to_compile_subresources[p]) {
				m_load_hook.continueLoad(*r);
			}
			m_to_compile_subresources.erase(p);
		}

		bool changed = false;
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
				char tmp[MAX_PATH_LENGTH];
				copyNString(Span(tmp), path_obj.c_str(), path_obj.length() - 5);
				path_obj = tmp;
			}

			const Array<Path> removed_subresources = removeResource(path_obj.c_str());
			addResource(path_obj.c_str());
			reloadSubresources(removed_subresources);

			auto iter = m_dependencies.find(path_obj);
			if (iter.isValid()) {
				const Array<Path> tmp(iter.value());
				m_dependencies.erase(iter);
				for (Path& p : tmp) {
					Array<Path> removed_subresources = removeResource(p.c_str());
					addResource(p.c_str());
					reloadSubresources(removed_subresources);
				}
			}
			changed = true;
		}
		if (changed) {
			m_on_list_changed.invoke();
		}
	}

	void removeResource(const Path& path) override {
		m_resources.erase(path.getHash());
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
			const u32 hash = crc32(*i);
			MutexGuard lock(m_plugin_mutex);
			m_plugins.insert(hash, &plugin);
			++i;
		}
	}

	void unlockResources() override {
		m_resources_mutex.exit();
	}

	const HashMap<u32, ResourceItem, HashFuncDirect<u32>>& lockResources() override {
		m_resources_mutex.enter();
		return m_resources;
	}

	Semaphore m_semaphore;
	Mutex m_to_compile_mutex;
	Mutex m_compiled_mutex;
	Mutex m_plugin_mutex;
	Mutex m_changed_mutex;
	HashMap<Path, Array<Resource*>> m_to_compile_subresources; 
	HashMap<Path, Array<Path>> m_dependencies;
	Array<Path> m_changed_files;
	Array<Path> m_to_compile;
	Array<Path> m_compiled;
	StudioApp& m_app;
	LoadHook m_load_hook;
	HashMap<u32, IPlugin*, HashFuncDirect<u32>> m_plugins;
	AssetCompilerTask m_task;
	FileSystemWatcher* m_watcher;
	Mutex m_resources_mutex;
	HashMap<u32, ResourceItem, HashFuncDirect<u32>> m_resources;
	HashMap<u32, ResourceType, HashFuncDirect<u32>> m_registered_extensions;
	DelegateList<void()> m_on_list_changed;

	u32 m_compile_batch_count = 0;
	u32 m_batch_remaining_count = 0;
	StaticString<MAX_PATH_LENGTH> m_res_in_progress;
};


int AssetCompilerTask::task()
{
	while (!m_finished) {
		m_compiler.m_semaphore.wait();
		const Path p = [&]{
			MutexGuard lock(m_compiler.m_to_compile_mutex);
			Path p = m_compiler.m_to_compile.back();
			m_compiler.m_res_in_progress = p.c_str();
			m_compiler.m_to_compile.pop();
			return p;
		}();
		if (p.isValid()) {
			PROFILE_BLOCK("compile asset");
			Profiler::pushString(p.c_str());
			logInfo("Editor") << "Compiling " << p << "...";
			const bool compiled = m_compiler.compile(p);
			if (!compiled) {
				logError("Editor") << "Failed to compile resource " << p;
			}
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

