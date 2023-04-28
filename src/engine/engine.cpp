#include "engine/allocators.h"
#include "engine/atomic.h"
#include "engine/core.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/hash.h"
#include "engine/input_system.h"
#include "engine/plugin.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/page_allocator.h"
#include "engine/path.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "engine/world.h"
#include <lua.hpp>

namespace Lumix
{

static const u32 SERIALIZED_PROJECT_MAGIC = 0x5f50524c; // == '_PRL'

void registerEngineAPI(lua_State* L, Engine* engine);

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
		, m_smooth_time_delta(1/60.f)
		, m_time_multiplier(1.0f)
		, m_paused(false)
		, m_next_frame(false)
	{
		for (float& f : m_last_time_deltas) f = 1/60.f;
		os::init();
		registerLogCallback<&EngineImpl::logToFile>(this);
		registerLogCallback<logToDebugOutput>();

		os::InitWindowArgs init_win_args;
		init_win_args.handle_file_drops = init_data.handle_file_drops;
		init_win_args.name = init_data.window_title;
		m_window_handle = os::createWindow(init_win_args);
		if (m_window_handle == os::INVALID_WINDOW) {
			logError("Failed to create main window.");
		}

		m_is_log_file_open = m_log_file.open("lumix.log");
		
		profiler::setThreadName("Worker");
		installUnhandledExceptionHandler();

		logInfo("Creating engine...");
		if (init_data.working_dir) logInfo("Working directory: ", init_data.working_dir);
		char cmd_line[2048] = "";
		os::getCommandLine(Span(cmd_line));
		logInfo("Command line: ", cmd_line);

		os::logInfo();

		m_state = luaL_newstate();
		#ifdef _WIN32
			lua_setallocf(m_state, luaAlloc, this);
		#endif
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

		m_system_manager = SystemManager::create(*this);
		m_input_system = InputSystem::create(*this);

		logInfo("Engine created.");

		SystemManager::createAllStatic(*this);

		m_system_manager->addSystem(createCorePlugin(*this), nullptr);

		#ifdef LUMIXENGINE_PLUGINS
			const char* plugins[] = { LUMIXENGINE_PLUGINS };
			for (auto* plugin_name : plugins) {
				if (plugin_name[0] && !m_system_manager->load(plugin_name)) {
					logInfo(plugin_name, " plugin has not been loaded");
				}
			}
		#endif

		for (auto* plugin_name : init_data.plugins) {
			if (plugin_name[0] && !m_system_manager->load(plugin_name)) {
				logInfo(plugin_name, " plugin has not been loaded");
			}
		}

		m_system_manager->initSystems();
	}

	~EngineImpl()
	{
		m_prefab_resource_manager.destroy();
		for (Resource* res : m_lua_resources) {
			res->decRefCount();
		}

		m_system_manager.reset();
		m_input_system.reset();
		m_file_system.reset();

		lua_close(m_state);

		unregisterLogCallback<&EngineImpl::logToFile>(this);
		m_log_file.close();
		m_is_log_file_open = false;
		os::destroyWindow(m_window_handle);
	}

	static void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
		EngineImpl* engine = (EngineImpl*)ud;
		engine->m_lua_allocated = engine->m_lua_allocated + nsize - osize;
		if (nsize == 0) {
			if (osize > 0) engine->m_lua_allocator.deallocate(ptr);
			return nullptr;
		}
		if (!ptr) return engine->m_lua_allocator.allocate(nsize);

		return engine->m_lua_allocator.reallocate(ptr, nsize);
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

	bool instantiatePrefab(World& world,
		const struct PrefabResource& prefab,
		const struct DVec3& pos,
		const struct Quat& rot,
		const Vec3& scale,
		EntityMap& entity_map) override
	{
		ASSERT(prefab.isReady());
		InputMemoryStream blob(prefab.data);
		if (!world.deserialize(blob, entity_map)) {
			logError("Failed to instantiate prefab ", prefab.getPath());
			return false;
		}

		ASSERT(!entity_map.m_map.empty());
		const EntityRef root = (EntityRef)entity_map.m_map[0];
		ASSERT(!world.getParent(root).isValid());
		ASSERT(!world.getNextSibling(root).isValid());
		world.setTransform(root, pos, rot, scale);
		return true;
	}

	World& createWorld(bool is_main_world) override {
		World* world = LUMIX_NEW(m_allocator, World)(*this, m_allocator);

		if (is_main_world) {
			lua_State* L = m_state;
			lua_getglobal(L, "Lumix"); // [ Lumix ]
			lua_getfield(L, -1, "World"); // [ Lumix, World ]
			lua_getfield(L, -1, "new"); // [ Lumix, World, new ]
			lua_insert(L, -2); // [ Lumix, new, World ]
			lua_pushlightuserdata(L, world); // [ Lumix, new, World, c_world]
			lua_call(L, 2, 1); // [ Lumix, world ]
			lua_setfield(L, -2, "main_world");
			lua_pop(L, 1);
		}

		return *world;
	}


	void destroyWorld(World& world) override
	{
		LUMIX_DELETE(m_allocator, &world);
		m_resource_manager.removeUnreferenced();
	}


	void startGame(World& world) override
	{
		ASSERT(!m_is_game_running);
		m_is_game_running = true;
		for (UniquePtr<IModule>& module : world.getModules()) {
			module->startGame();
		}
		for (auto* system : m_system_manager->getSystems()) {
			system->startGame();
		}
	}


	void stopGame(World& world) override
	{
		ASSERT(m_is_game_running);
		m_is_game_running = false;
		for (UniquePtr<IModule>& module : world.getModules()) {
			module->stopGame();
		}
		for (auto* system : m_system_manager->getSystems()) {
			system->stopGame();
		}
	}

	bool isPaused() const override {
		return m_paused;
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

	void computeSmoothTimeDelta() {
		float tmp[11];
		memcpy(tmp, m_last_time_deltas, sizeof(tmp));
		qsort(tmp, lengthOf(tmp), sizeof(tmp[0]), [](const void* a, const void* b) -> i32 {
			return *(const float*)a < *(const float*)b ? -1 : *(const float*)a > *(const float*)b ? 1 : 0;
		});
		float t = 0;
		for (u32 i = 2; i < lengthOf(tmp) - 2; ++i) {
			t += tmp[i];
		}
		m_smooth_time_delta = t / (lengthOf(tmp) - 4);
		static u32 counter = profiler::createCounter("Smooth time delta (ms)", 0);
		profiler::pushCounter(counter, m_smooth_time_delta * 1000.f);
	}

	void update(World& world) override
	{
		PROFILE_FUNCTION();
		static u32 lua_mem_counter = profiler::createCounter("Lua Memory (KB)", 0);
		profiler::pushCounter(lua_mem_counter, float(double(m_lua_allocated) / 1024.0));

		if (m_allocator.isDebug()) {
			debug::Allocator& a = (debug::Allocator&)m_allocator;
			static u32 mem_counter = profiler::createCounter("Main allocator (MB)", 0);
			profiler::pushCounter(mem_counter, float(double(a.getTotalSize()) / (1024.0 * 1024.0)));
		}

		const float reserved_pages_size = (m_page_allocator.getReservedCount() * PageAllocator::PAGE_SIZE) / (1024.f * 1024.f);
		static u32 page_allocator_counter = profiler::createCounter("Page allocator (MB)", 0);
		profiler::pushCounter(page_allocator_counter , reserved_pages_size);
		
		#ifdef _WIN32
			const float process_mem = os::getProcessMemory() / (1024.f * 1024.f);
			static u32 process_mem_counter = profiler::createCounter("Process Memory (MB)", 0);
			profiler::pushCounter(process_mem_counter, process_mem);
		#endif

		float dt = m_timer.tick() * m_time_multiplier;
		if (m_next_frame) dt = 1 / 30.0f;
		++m_last_time_deltas_frame;
		m_last_time_deltas[m_last_time_deltas_frame % lengthOf(m_last_time_deltas)] = dt;
		static u32 counter = profiler::createCounter("Raw time delta (ms)", 0);
		profiler::pushCounter(counter, dt * 1000.f);

		computeSmoothTimeDelta();

		if (!m_paused || m_next_frame) {
			{
				PROFILE_BLOCK("update modules");
				for (UniquePtr<IModule>& module : world.getModules())
				{
					module->update(dt);
				}
			}
			{
				PROFILE_BLOCK("late update modules");
				for (UniquePtr<IModule>& module : world.getModules())
				{
					module->lateUpdate(dt);
				}
			}
			m_system_manager->update(dt);
		}
		m_input_system->update(dt);
		m_file_system->processCallbacks();
		m_next_frame = false;
	}

	enum class ProjectVersion : u32 {
		FIRST,
		HASH64,

		LAST,
	};

	struct ProjectHeader {
		u32 magic;
		ProjectVersion version;
	};

	DeserializeProjectResult deserializeProject(InputMemoryStream& serializer, Span<char> startup_world) override {
		ProjectHeader header;
		serializer.read(header);
		if (header.magic != SERIALIZED_PROJECT_MAGIC) return DeserializeProjectResult::CORRUPTED_FILE;
		if (header.version > ProjectVersion::LAST) return DeserializeProjectResult::VERSION_NOT_SUPPORTED;
		if (header.version <= ProjectVersion::HASH64) return DeserializeProjectResult::VERSION_NOT_SUPPORTED;
		const char* tmp = serializer.readString();
		copyString(startup_world, tmp);
		i32 count = 0;
		serializer.read(count);
		const Array<ISystem*>& systems = m_system_manager->getSystems();
		for (i32 i = 0; i < count; ++i) {
			StableHash hash;
			serializer.read(hash);
			i32 idx = systems.find([&](ISystem* system){
				return StableHash(system->getName()) == hash;
			});
			if (idx < 0) return DeserializeProjectResult::PLUGIN_NOT_FOUND;
			ISystem* system = systems[idx];
			if (!system) return DeserializeProjectResult::PLUGIN_NOT_FOUND;
			i32 version;
			serializer.read(version);
			if (version > system->getVersion()) return DeserializeProjectResult::PLUGIN_VERSION_NOT_SUPPORTED;
			if (!system->deserialize(version, serializer)) return DeserializeProjectResult::PLUGIN_DESERIALIZATION_FAILED;
		}
		return DeserializeProjectResult::SUCCESS;
	}

	void serializeProject(OutputMemoryStream& serializer, const char* startup_world) const override {
		ProjectHeader header;
		header.magic = SERIALIZED_PROJECT_MAGIC;
		header.version = ProjectVersion::LAST;
		serializer.write(header);
		serializer.writeString(startup_world);
		const Array<ISystem*>& systems = m_system_manager->getSystems();
		serializer.write((i32)systems.size());
		for (ISystem* system : systems) {
			const StableHash hash(system->getName());
			serializer.write(hash);
			serializer.write(system->getVersion());
			system->serialize(serializer);
		}
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

	SystemManager& getSystemManager() override { return *m_system_manager; }
	FileSystem& getFileSystem() override { return *m_file_system; }
	InputSystem& getInputSystem() override { return *m_input_system; }
	ResourceManagerHub& getResourceManager() override { return m_resource_manager; }
	lua_State* getState() override { return m_state; }
	float getLastTimeDelta() const override { return m_smooth_time_delta / m_time_multiplier; }

private:
	IAllocator& m_allocator;
	// lua callstacks are incomplete and they polute memory report if using main allocator 
	DefaultAllocator m_lua_allocator; 
	size_t m_lua_allocated = 0;
	PageAllocator m_page_allocator;
	UniquePtr<FileSystem> m_file_system;
	ResourceManagerHub m_resource_manager;
	UniquePtr<SystemManager> m_system_manager;
	PrefabResourceManager m_prefab_resource_manager;
	UniquePtr<InputSystem> m_input_system;
	os::Timer m_timer;
	float m_time_multiplier;
	float m_last_time_deltas[11] = {};
	u32 m_last_time_deltas_frame = 0;
	float m_smooth_time_delta;
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
