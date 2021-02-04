#include "engine/atomic.h"
#include "engine/crc32.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/input_system.h"
#include "engine/plugin.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math.h"
#include "engine/page_allocator.h"
#include "engine/path.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/universe.h"


namespace Lumix
{

void registerEngineAPI(lua_State* L, Engine* engine);

static const u32 SERIALIZED_ENGINE_MAGIC = 0x5f4c454e; // == '_LEN'
static const u32 SERIALIZED_PROJECT_MAGIC = 0x5f50524c; // == '_PRL'


#pragma pack(1)
struct SerializedEngineHeader
{
	u32 magic;
	u32 version;
};
#pragma pack()


struct PrefabResourceManager final : ResourceManager
{
	explicit PrefabResourceManager(IAllocator& allocator)
		: m_allocator(allocator)
		, ResourceManager(allocator)
	{}

	Resource* createResource(const Path& path) override
	{
		return LUMIX_NEW(m_allocator, PrefabResource)(path, *this, m_allocator);
	}

	void destroyResource(Resource& resource) override
	{
		return LUMIX_DELETE(m_allocator, &static_cast<PrefabResource&>(resource));
	}

	IAllocator& m_allocator;
};


struct EngineImpl final : Engine
{
public:
	void operator=(const EngineImpl&) = delete;
	EngineImpl(const EngineImpl&) = delete;

	EngineImpl(InitArgs&& init_data, IAllocator& allocator)
		: m_allocator(allocator)
		, m_prefab_resource_manager(m_allocator)
		, m_resource_manager(m_allocator)
		, m_lua_resources(m_allocator)
		, m_last_lua_resource_idx(-1)
		, m_is_game_running(false)
		, m_last_time_delta(0)
		, m_time_multiplier(1.0f)
		, m_paused(false)
		, m_next_frame(false)
	{
		os::init();
		os::InitWindowArgs init_win_args;
		init_win_args.fullscreen = init_data.fullscreen;
		init_win_args.handle_file_drops = init_data.handle_file_drops;
		init_win_args.name = init_data.window_title;
		m_window_handle = os::createWindow(init_win_args);
		if (m_window_handle == os::INVALID_WINDOW) {
			logError("Failed to create main window.");
		}

		m_is_log_file_open = m_log_file.open("lumix.log");
		
		logInfo("Creating engine...");
		profiler::setThreadName("Worker");
		installUnhandledExceptionHandler();

		registerLogCallback<&EngineImpl::logToFile>(this);
		registerLogCallback<logToDebugOutput>();

		os::logVersion();

		m_state = luaL_newstate();
		luaL_openlibs(m_state);

		registerEngineAPI(m_state, this);

		if (init_data.file_system.get()) {
			m_file_system = static_cast<UniquePtr<FileSystem>&&>(init_data.file_system);
		}
		else if (init_data.working_dir) {
			m_file_system = FileSystem::create(init_data.working_dir, m_allocator);
		}
		else {
			char current_dir[LUMIX_MAX_PATH];
			os::getCurrentDirectory(Span(current_dir)); 
			m_file_system = FileSystem::create(current_dir, m_allocator);
		}

		m_resource_manager.init(*m_file_system);
		m_prefab_resource_manager.create(PrefabResource::TYPE, m_resource_manager);

		m_plugin_manager = PluginManager::create(*this);
		m_input_system = InputSystem::create(*this);

		logInfo("Engine created.");

		PluginManager::createAllStatic(*this);

		#ifdef LUMIXENGINE_PLUGINS
			const char* plugins[] = { LUMIXENGINE_PLUGINS };
			for (auto* plugin_name : plugins) {
				if (plugin_name[0] && !m_plugin_manager->load(plugin_name)) {
					logInfo(plugin_name, " plugin has not been loaded");
				}
			}
		#endif

		for (auto* plugin_name : init_data.plugins) {
			if (plugin_name[0] && !m_plugin_manager->load(plugin_name)) {
				logInfo(plugin_name, " plugin has not been loaded");
			}
		}

		m_plugin_manager->initPlugins();
	}

	~EngineImpl()
	{
		for (Resource* res : m_lua_resources) {
			res->decRefCount();
		}

		m_plugin_manager.reset();
		m_input_system.reset();
		m_file_system.reset();

		m_prefab_resource_manager.destroy();
		lua_close(m_state);

		unregisterLogCallback<&EngineImpl::logToFile>(this);
		m_log_file.close();
		m_is_log_file_open = false;
		os::destroyWindow(m_window_handle);
	}

	static void logToDebugOutput(LogLevel level, const char* message)
	{
		if(level == LogLevel::ERROR) {
			debug::debugOutput("Error: ");
		}
		debug::debugOutput(message);
		debug::debugOutput("\n");
	}

	void logToFile(LogLevel level, const char* message)
	{
		if (!m_is_log_file_open) return;
		bool success = true;
		if (level == LogLevel::ERROR) {
			success = m_log_file.write("Error: ", stringLength("Error :"));
		}
		success = m_log_file.write(message, stringLength(message)) && success ;
		success = m_log_file.write("\n", 1) && success ;
		ASSERT(success);
		m_log_file.flush();
	}

	os::WindowHandle getWindowHandle() override { return m_window_handle; }
	IAllocator& getAllocator() override { return m_allocator; }
	PageAllocator& getPageAllocator() override { return m_page_allocator; }

	bool instantiatePrefab(Universe& universe,
		const struct PrefabResource& prefab,
		const struct DVec3& pos,
		const struct Quat& rot,
		float scale,
		Ref<EntityMap> entity_map) override
	{
		ASSERT(prefab.isReady());
		InputMemoryStream blob(prefab.data);
		if (!deserialize(universe, blob, entity_map)) {
			logError("Failed to instantiate prefab ", prefab.getPath());
			return false;
		}

		ASSERT(!entity_map->m_map.empty());
		const EntityRef root = (EntityRef)entity_map->m_map[0];
		ASSERT(!universe.getParent(root).isValid());
		ASSERT(!universe.getNextSibling(root).isValid());
		universe.setTransform(root, pos, rot, scale);
		return true;
	}

	Universe& createUniverse(bool is_main_universe) override
	{
		Universe* universe = LUMIX_NEW(m_allocator, Universe)(*this, m_allocator);
		const Array<IPlugin*>& plugins = m_plugin_manager->getPlugins();
		for (auto* plugin : plugins) {
			plugin->createScenes(*universe);
		}

		for (UniquePtr<IScene>& scene : universe->getScenes()) {
			scene->init();
		}

		if (is_main_universe) {
			lua_State* L = m_state;
			LuaWrapper::DebugGuard guard(L);
			lua_getglobal(L, "Lumix"); // [ Lumix ]
			lua_getfield(L, -1, "Universe"); // [ Lumix, Universe ]
			lua_getfield(L, -1, "new"); // [ Lumix, Universe, new ]
			lua_insert(L, -2); // [ Lumix, new, Universe ]
			lua_pushlightuserdata(L, universe); // [ Lumix, new, Universe, c_universe ]
			lua_call(L, 2, 1); // [ Lumix, universe ]
			lua_setfield(L, -2, "main_universe");
			lua_pop(L, 1);
		}

		return *universe;
	}


	void destroyUniverse(Universe& universe) override
	{
		Array<UniquePtr<IScene>>& scenes = universe.getScenes();
		while (!scenes.empty()) {
			UniquePtr<IScene>& scene = scenes.back();
			scene->clear();
			scenes.pop();
		}
		LUMIX_DELETE(m_allocator, &universe);
		m_resource_manager.removeUnreferenced();
	}


	void startGame(Universe& context) override
	{
		ASSERT(!m_is_game_running);
		m_is_game_running = true;
		for (UniquePtr<IScene>& scene : context.getScenes())
		{
			scene->startGame();
		}
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			plugin->startGame();
		}
	}


	void stopGame(Universe& context) override
	{
		ASSERT(m_is_game_running);
		m_is_game_running = false;
		for (UniquePtr<IScene>& scene : context.getScenes())
		{
			scene->stopGame();
		}
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			plugin->stopGame();
		}
	}


	void pause(bool pause) override
	{
		m_paused = pause;
	}


	void nextFrame() override
	{
		m_next_frame = true;
	}


	void setTimeMultiplier(float multiplier) override
	{
		m_time_multiplier = maximum(multiplier, 0.001f);
	}


	void update(Universe& context) override
	{
		PROFILE_FUNCTION();
		float dt = m_timer.tick() * m_time_multiplier;
		if (m_next_frame)
		{
			m_paused = false;
			dt = 1 / 30.0f;
		}
		m_last_time_delta = dt;
		{
			PROFILE_BLOCK("update scenes");
			for (UniquePtr<IScene>& scene : context.getScenes())
			{
				scene->update(dt, m_paused);
			}
		}
		{
			PROFILE_BLOCK("late update scenes");
			for (UniquePtr<IScene>& scene : context.getScenes())
			{
				scene->lateUpdate(dt, m_paused);
			}
		}
		m_plugin_manager->update(dt, m_paused);
		m_input_system->update(dt);
		m_file_system->processCallbacks();

		if (m_next_frame)
		{
			m_paused = true;
			m_next_frame = false;
		}
	}


	void serializeSceneVersions(OutputMemoryStream& serializer, Universe& ctx)
	{
		serializer.write(ctx.getScenes().size());
		for (UniquePtr<IScene>& scene : ctx.getScenes())
		{
			serializer.write(crc32(scene->getPlugin().getName()));
			serializer.write(scene->getVersion());
		}
	}


	void serializePluginList(OutputMemoryStream& serializer)
	{
		serializer.write((i32)m_plugin_manager->getPlugins().size());
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			serializer.writeString(plugin->getName());
		}
	}


	bool hasSerializedPlugins(InputMemoryStream& serializer)
	{
		i32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			const char* tmp = serializer.readString();
			if (!m_plugin_manager->getPlugin(tmp))
			{
				logError("Missing plugin ", tmp);
				return false;
			}
		}
		return true;
	}

	bool deserializeProject(InputMemoryStream& serializer) override {
		SerializedEngineHeader header;
		serializer.read(header);
		if (header.magic != SERIALIZED_PROJECT_MAGIC) return false;
		if (header.version != 0) return false;
		i32 count = 0;
		serializer.read(count);
		const Array<IPlugin*>& plugins = m_plugin_manager->getPlugins();
		for (i32 i = 0; i < count; ++i) {
			u32 hash;
			serializer.read(hash);
			i32 idx = plugins.find([&](IPlugin* plugin){
				return crc32(plugin->getName()) == hash;
			});
			if (idx < 0) return false;
			IPlugin* plugin = plugins[idx];
			if (!plugin) return false;
			u32 version;
			serializer.read(version);
			if (!plugin->deserialize(version, serializer)) return false;
		}
		return false;
	}

	void serializeProject(OutputMemoryStream& serializer) const override {
		SerializedEngineHeader header;
		header.magic = SERIALIZED_PROJECT_MAGIC;
		header.version = 0;
		serializer.write(header);
		const Array<IPlugin*>& plugins = m_plugin_manager->getPlugins();
		serializer.write((i32)plugins.size());
		for (IPlugin* plugin : plugins) {
			const u32 hash = crc32(plugin->getName());
			serializer.write(hash);
			serializer.write((u32)plugin->getVersion());
			plugin->serialize(serializer);
		}
	}

	u32 serialize(Universe& ctx, OutputMemoryStream& serializer) override
	{
		SerializedEngineHeader header;
		header.magic = SERIALIZED_ENGINE_MAGIC; // == '_LEN'
		header.version = 0;
		serializer.write(header);
		serializePluginList(serializer);
		//serializeSceneVersions(serializer, ctx);
		i32 pos = (i32)serializer.size();
		ctx.serialize(serializer);
		serializer.write((i32)ctx.getScenes().size());
		for (UniquePtr<IScene>& scene : ctx.getScenes()) {
			serializer.writeString(scene->getPlugin().getName());
			serializer.write(scene->getVersion());
			scene->serialize(serializer);
		}
		u32 crc = crc32((const u8*)serializer.data() + pos, (i32)serializer.size() - pos);
		return crc;
	}


	bool deserialize(Universe& ctx, InputMemoryStream& serializer, Ref<EntityMap> entity_map) override
	{
		SerializedEngineHeader header;
		serializer.read(header);
		if (header.magic != SERIALIZED_ENGINE_MAGIC)
		{
			logError("Wrong or corrupted file");
			return false;
		}
		if (!hasSerializedPlugins(serializer)) return false;

		ctx.deserialize(serializer, entity_map);
		i32 scene_count;
		serializer.read(scene_count);
		for (int i = 0; i < scene_count; ++i)
		{
			const char* tmp = serializer.readString();
			IScene* scene = ctx.getScene(crc32(tmp));
			const i32 version = serializer.read<i32>();
			scene->deserialize(serializer, entity_map, version);
		}
		return true;
	}


	void unloadLuaResource(LuaResourceHandle resource) override
	{
		auto iter = m_lua_resources.find(resource);
		if (!iter.isValid()) return;
		Resource* res = iter.value();
		m_lua_resources.erase(iter);
		res->decRefCount();
	}


	LuaResourceHandle addLuaResource(const Path& path, ResourceType type) override
	{
		Resource* res = m_resource_manager.load(type, path);
		if (!res) return 0xffFFffFF;
		++m_last_lua_resource_idx;
		ASSERT(m_last_lua_resource_idx != 0xffFFffFF);
		m_lua_resources.insert(m_last_lua_resource_idx, res);
		return m_last_lua_resource_idx;
	}


	Resource* getLuaResource(LuaResourceHandle resource) const override
	{
		auto iter = m_lua_resources.find(resource);
		if (iter.isValid()) return iter.value();
		return nullptr;
	}

	PluginManager& getPluginManager() override { return *m_plugin_manager; }
	FileSystem& getFileSystem() override { return *m_file_system; }
	InputSystem& getInputSystem() override { return *m_input_system; }
	ResourceManagerHub& getResourceManager() override { return m_resource_manager; }
	lua_State* getState() override { return m_state; }
	float getLastTimeDelta() const override { return m_last_time_delta / m_time_multiplier; }

private:
	IAllocator& m_allocator;
	PageAllocator m_page_allocator;
	UniquePtr<FileSystem> m_file_system;
	ResourceManagerHub m_resource_manager;
	UniquePtr<PluginManager> m_plugin_manager;
	PrefabResourceManager m_prefab_resource_manager;
	UniquePtr<InputSystem> m_input_system;
	os::Timer m_timer;
	float m_time_multiplier;
	float m_last_time_delta;
	bool m_is_game_running;
	bool m_paused;
	bool m_next_frame;
	os::WindowHandle m_window_handle;
	lua_State* m_state;
	os::OutputFile m_log_file;
	bool m_is_log_file_open = false;
	HashMap<int, Resource*> m_lua_resources;
	u32 m_last_lua_resource_idx;
};


UniquePtr<Engine> Engine::create(InitArgs&& init_data, IAllocator& allocator)
{
	return UniquePtr<EngineImpl>::create(allocator, static_cast<InitArgs&&>(init_data), allocator);
}


} // namespace Lumix
