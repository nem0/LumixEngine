#include "asset_compiler.h"
#include "editor/file_system_watcher.h"
#include "editor/log_ui.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/mt/atomic.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/os.h"
#include "engine/path_utils.h"
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


struct AssetCompilerTask : MT::Task
{
	AssetCompilerTask(AssetCompilerImpl& compiler, IAllocator& allocator) 
		: MT::Task(allocator) 
		 , m_compiler(compiler)
	{}

	int task() override;

	volatile int m_to_compile_count = 0;
	AssetCompilerImpl& m_compiler;
	volatile bool m_finished = false;
};


void AssetCompiler::IPlugin::addSubresources(AssetCompiler& compiler, const char* path, HashMap<ResourceType, Array<Path>, HashFunc<ResourceType>>& subresources)
{
	const ResourceType type = compiler.getResourceType(path);
	if (type == INVALID_RESOURCE_TYPE) return;
	
	const Path path_obj(path);
	auto iter = subresources.find(type);
	if (!iter.isValid()) return;
	if (iter.value().indexOf(path_obj) < 0) iter.value().push(path_obj);
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
		, m_plugins(app.getWorldEditor().getAllocator())
		, m_task(*this, app.getWorldEditor().getAllocator())
		, m_to_compile(app.getWorldEditor().getAllocator())
		, m_compiled(app.getWorldEditor().getAllocator())
		, m_semaphore(0, 0x7fFFffFF)
		, m_registered_extensions(app.getWorldEditor().getAllocator())
		, m_resources(app.getWorldEditor().getAllocator())
		, m_to_compile_subresources(app.getWorldEditor().getAllocator())
		, m_dependencies(app.getWorldEditor().getAllocator())
	{
		m_watcher = FileSystemWatcher::create(".", app.getWorldEditor().getAllocator());
		m_watcher->getCallback().bind<AssetCompilerImpl, &AssetCompilerImpl::onFileChanged>(this);
		m_task.create("Asset compiler", true);
		const char* base_path = m_app.getWorldEditor().getEngine().getFileSystem().getBasePath();
		StaticString<MAX_PATH_LENGTH> path(base_path, ".lumix/assets");
		OS::makePath(path);
		ResourceManagerHub& rm = app.getWorldEditor().getEngine().getResourceManager();
		rm.setLoadHook(&m_load_hook);
	}

	~AssetCompilerImpl()
	{
		OS::OutputFile file;
		FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
		// TODO make this safe - i.e. handle case when program gets interrupted while writing the file
		if (fs.open(".lumix/assets/_list.txt", &file)) {
			file << "resources = {\n";
			for (auto& i : m_resources) {
				for (const Path& j : i) {
					file << "\"" << j.c_str() << "\",\n";
				}
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
		}
		else {
			logError("Editor") << "Could not save .lumix/assets/_list.txt";
		}

		ASSERT(m_plugins.empty());
		m_task.m_finished = true;
		m_to_compile.emplace();
		m_semaphore.signal();
		m_task.destroy();
		ResourceManagerHub& rm = m_app.getWorldEditor().getEngine().getResourceManager();
		rm.setLoadHook(nullptr);
		FileSystemWatcher::destroy(m_watcher);
	}


	ResourceType getResourceType(const char* path) const override
	{
		char ext[16];
		PathUtils::getExtension(ext, lengthOf(ext), path);

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

		IAllocator& allocator = m_app.getWorldEditor().getAllocator();
		if (!m_resources.find(type).isValid()) {
			m_resources.insert(type, Array<Path>(allocator));
		}
	}


	void addResource(const char* fullpath)
	{
		char ext[10];
		PathUtils::getExtension(ext, sizeof(ext), fullpath);
		makeLowercase(ext, lengthOf(ext), ext);
	
		auto iter = m_plugins.find(crc32(ext));
		if (!iter.isValid()) return;

		iter.value()->addSubresources(*this, fullpath, m_resources);
	}

	
	void processDir(const char* dir, int base_length, u64 list_last_modified)
	{
		FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
		auto* iter = fs.createFileIterator(dir);
		OS::FileInfo info;
		while (getNextFile(iter, &info))
		{
			if (info.filename[0] == '.') continue;

			if (info.is_directory)
			{
				char child_path[MAX_PATH_LENGTH];
				copyString(child_path, dir);
				catString(child_path, "/");
				catString(child_path, info.filename);
				processDir(child_path, base_length, list_last_modified);
			}
			else
			{
				char fullpath[MAX_PATH_LENGTH];
				copyString(fullpath, dir + base_length);
				catString(fullpath, "/");
				catString(fullpath, info.filename);

				if (fs.getLastModified(fullpath[0] == '/' ? fullpath + 1 : fullpath) > list_last_modified) {
					addResource(fullpath);
				}
			}
		}

		destroyFileIterator(iter);
	}


	void registerDependency(const Path& included_from, const Path& dependency) override
	{
		auto iter = m_dependencies.find(dependency);
		if (!iter.isValid()) {
			IAllocator& allocator = m_app.getWorldEditor().getAllocator();
			m_dependencies.insert(dependency, Array<Path>(allocator));
			iter = m_dependencies.find(dependency);
		}
		iter.value().push(included_from);
	}


	void onInitFinished() override
	{
		OS::InputFile file;
		const char* list_path = ".lumix/assets/_list.txt";
		FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
		if (fs.open(list_path, &file)) {
			Array<char> content(m_app.getWorldEditor().getAllocator());
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

				LuaWrapper::forEachArrayItem<Path>(L, -1, "array of strings expected", [this](const Path& p){
					const ResourceType type = getResourceType(p.c_str());
					if (type != INVALID_RESOURCE_TYPE) {
						auto iter = m_resources.find(type);
						if (iter.isValid() && iter.value().indexOf(p) < 0) {
							iter.value().push(p);
						}
					}	
				});
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
					IAllocator& allocator = m_app.getWorldEditor().getAllocator();
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
		processDir("", 0, list_last_modified);
	}


	Array<Path> removeResource(const char* path)
	{
		Array<Path> res(m_app.getWorldEditor().getAllocator());
		for (Array<Path>& tmp : m_resources) {
			for (int i = tmp.size() - 1; i >= 0; --i) {
				const Path& p = tmp[i];
				if (equalStrings(getResourceFilePath(p.c_str()), path)) {
					res.push(p);
					tmp.erase(i);
				}
			}
		}

		const Path path_obj(path);
		for (Array<Path>& deps : m_dependencies) {
			deps.eraseItems([&](const Path& p){ return p == path_obj; });
		}

		return res;
	}


	void reloadSubresources(const Array<Path>& subresources)
	{
		ResourceManagerHub& rman = m_app.getWorldEditor().getEngine().getResourceManager();
		for (const Path& p : subresources) {
			rman.reload(p);
		}
	}


	void onFileChanged(const char* path)
	{
		if (startsWith(path, ".lumix")) return;
		
		Path path_obj(path);

		const Array<Path> removed_subresources = removeResource(path_obj.c_str());
		addResource(path);
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
	}

	bool getMeta(const Path& res, void* user_ptr, void (*callback)(void*, lua_State*)) const override
	{
		const PathUtils::FileInfo info(res.c_str());
		OS::InputFile file;
		const StaticString<MAX_PATH_LENGTH> meta_path(info.m_dir, info.m_basename, ".meta");
		
		FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
		if (!fs.open(meta_path, &file)) return nullptr;

		Array<char> buf(m_app.getWorldEditor().getAllocator());
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
		const PathUtils::FileInfo info(res.c_str());
		OS::OutputFile file;
		const StaticString<MAX_PATH_LENGTH> meta_path(info.m_dir, info.m_basename, ".meta");
				
		FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
		if (!fs.open(meta_path, &file)) {
			logError("Editor") << "Could not create " << meta_path;
			return;
		}

		file.write(src, stringLength(src));
		file.close();
	}


	bool compile(const Path& src) override
	{
		char ext[16];
		PathUtils::getExtension(ext, lengthOf(ext), src.c_str());
		const u32 hash = crc32(ext);
		MT::SpinLock lock(m_plugin_mutex);
		auto iter = m_plugins.find(hash);
		if (!iter.isValid()) return false;
		return iter.value()->compile(src);
	}
	

	static const char* getResourceFilePath(const char* str)
	{
		const char* c = str;
		while (*c && *c != ':') ++c;
		return *c != ':' ? str : c + 1;
	}


	ResourceManagerHub::LoadHook::Action onBeforeLoad(Resource& res)
	{
		const char* filepath = getResourceFilePath(res.getPath().c_str());

		FileSystem& fs = m_app.getWorldEditor().getEngine().getFileSystem();
		if (!fs.fileExists(filepath)) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;
		if (startsWith(filepath, ".lumix/assets/")) return ResourceManagerHub::LoadHook::Action::IMMEDIATE;

		const u32 hash = res.getPath().getHash();
		const StaticString<MAX_PATH_LENGTH> dst_path(".lumix/assets/", hash, ".res");
		const PathUtils::FileInfo info(filepath);
		const StaticString<MAX_PATH_LENGTH> meta_path(info.m_dir, info.m_basename, ".meta");

		if (!fs.fileExists(dst_path)
			|| fs.getLastModified(dst_path) < fs.getLastModified(filepath)
			|| fs.getLastModified(dst_path) < fs.getLastModified(meta_path)
			)
		{
			MT::SpinLock lock(m_to_compile_mutex);
			MT::atomicIncrement(&m_task.m_to_compile_count);
			const Path path(filepath);
			auto iter = m_to_compile_subresources.find(path);
			if (!iter.isValid()) {
				m_to_compile.push(path);
				m_semaphore.signal();
				IAllocator& allocator = m_app.getWorldEditor().getAllocator();
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
		MT::SpinLock lock(m_compiled_mutex);
		if(m_compiled.empty()) return Path();
		const Path p = m_compiled.back();
		m_compiled.pop();
		return p;
	}
	
	void update() override
	{
		LogUI& log = m_app.getLogUI();
		if (m_log_id == -1 && m_task.m_to_compile_count > 0) {
			m_log_id = log.addNotification("Compiling resources...");
		}
		if(m_log_id != -1) {
			log.setNotificationTime(m_log_id, 3.f);
			if (m_task.m_to_compile_count == 0) m_log_id = -1;
		}
		if(m_task.m_to_compile_count == 0) {
			m_log_id = -1;
		}

		for(;;) {
			Path p = popCompiledResource();
			if (!p.isValid()) break;

			// this can take some time, spinmutex is probably not the best option
			MT::SpinLock lock(m_compiled_mutex);

			for (Resource* r : m_to_compile_subresources[p]) {
				m_load_hook.continueLoad(*r);
			}
			m_to_compile_subresources.erase(p);
		}
	}


	void removePlugin(IPlugin& plugin) override
	{
		MT::SpinLock lock(m_plugin_mutex);
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

	const char* getCompiledDir() const override { return ".lumix/assets/"; }


	void addPlugin(IPlugin& plugin, const char** extensions) override
	{
		const char** i = extensions;
		while(*i) {
			const u32 hash = crc32(*i);
			MT::SpinLock lock(m_plugin_mutex);
			m_plugins.insert(hash, &plugin);
			++i;
		}
	}


	const Array<Path>& getResources(ResourceType type) const override
	{
		return m_resources[type];
	}


	MT::Semaphore m_semaphore;
	MT::SpinMutex m_to_compile_mutex;
	MT::SpinMutex m_compiled_mutex;
	MT::SpinMutex m_plugin_mutex;
	HashMap<Path, Array<Resource*>> m_to_compile_subresources; 
	HashMap<Path, Array<Path>> m_dependencies;
	Array<Path> m_to_compile;
	Array<Path> m_compiled;
	StudioApp& m_app;
	LoadHook m_load_hook;
	HashMap<u32, IPlugin*> m_plugins;
	AssetCompilerTask m_task;
	FileSystemWatcher* m_watcher;
	HashMap<ResourceType, Array<Path>> m_resources;
	HashMap<u32, ResourceType> m_registered_extensions;
	int m_log_id = -1;
};


int AssetCompilerTask::task()
{
	while (!m_finished) {
		m_compiler.m_semaphore.wait();
		const Path p = [&]{
			MT::SpinLock lock(m_compiler.m_to_compile_mutex);
			Path p = m_compiler.m_to_compile.back();
			m_compiler.m_to_compile.pop();
			return p;
		}();
		if (p.isValid()) {
			PROFILE_BLOCK("compile asset");
			Profiler::pushString(p.c_str());
			const bool compiled = m_compiler.compile(p);
			MT::atomicDecrement(&m_to_compile_count);
			if (compiled) {
				MT::SpinLock lock(m_compiler.m_compiled_mutex);
				m_compiler.m_compiled.push(p);
			}
			else {
				logError("Editor") << "Failed to compile resource " << p;
			}
		}
	}
	return 0;
}


AssetCompiler* AssetCompiler::create(StudioApp& app)
{
	return LUMIX_NEW(app.getWorldEditor().getAllocator(), AssetCompilerImpl)(app);
}


void AssetCompiler::destroy(AssetCompiler& compiler)
{
	AssetCompilerImpl& impl = (AssetCompilerImpl&)compiler;
	IAllocator& allocator = impl.m_app.getWorldEditor().getAllocator();
	LUMIX_DELETE(allocator, &compiler);
}


} // namespace Lumix

