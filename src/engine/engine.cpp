#include "core/atomic.h"
#include "core/debug.h"
#include "core/hash.h"
#include "core/job_system.h"
#include "core/log_callback.h"
#include "core/math.h"
#include "core/os.h"
#include "core/page_allocator.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "core/stream.h"
#include "core/string.h"
#include "engine/core.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/input_system.h"
#include "engine/plugin.h"
#include "engine/prefab.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include <lz4/lz4.h>

namespace Lumix
{

static const u32 SERIALIZED_PROJECT_MAGIC = 0x5f50524c;

struct PrefabResourceManager final : ResourceManager {
	explicit PrefabResourceManager(IAllocator& allocator)
		: m_allocator(allocator)
		, ResourceManager(allocator)
	{}

	Resource* createResource(const Path& path) override {
		return LUMIX_NEW(m_allocator, PrefabResource)(path, *this, m_allocator);
	}

	void destroyResource(Resource& resource) override {
		return LUMIX_DELETE(m_allocator, &static_cast<PrefabResource&>(resource));
	}

	IAllocator& m_allocator;
};


struct EngineImpl final : Engine {
	void operator=(const EngineImpl&) = delete;
	EngineImpl(const EngineImpl&) = delete;

	EngineImpl(InitArgs&& init_data, IAllocator& allocator)
		: m_allocator(allocator, "engine")
		, m_page_allocator(m_allocator)
		, m_prefab_resource_manager(m_allocator)
		, m_resource_manager(*this, m_allocator)
		, m_is_game_running(false)
		, m_smooth_time_delta(1/60.f)
		, m_time_multiplier(1.0f)
		, m_paused(false)
		, m_next_frame(false)
	{
		PROFILE_FUNCTION();
		for (float& f : m_last_time_deltas) f = 1/60.f;
		os::init();
		registerLogCallback<&EngineImpl::logToFile>(this);
		registerLogCallback<logToDebugOutput>();

		m_is_log_file_open = m_log_file.open(init_data.log_path);
		
		installUnhandledExceptionHandler();

		logInfo("Creating engine...");
		if (init_data.working_dir) logInfo("Working directory: ", init_data.working_dir);
		char cmd_line[2048] = "";
		os::getCommandLine(Span(cmd_line));
		logInfo("Command line: ", cmd_line);

		os::logInfo();

		if (init_data.file_system.get()) {
			m_file_system = static_cast<UniquePtr<FileSystem>&&>(init_data.file_system);
		}
		else if (init_data.working_dir) {
			m_file_system = FileSystem::create(init_data.working_dir, m_allocator);
		}
		else {
			char current_dir[MAX_PATH];
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

		m_lz4_state = (u8*)m_allocator.allocate(LZ4_sizeofState(), 8);
	}

	void setMainWindow(os::WindowHandle wnd) override {
		m_window_handle = wnd;
	}

	void init() override {
		m_system_manager->initSystems();
	}

	~EngineImpl()
	{
		m_allocator.deallocate(m_lz4_state);
		
		for (ISystem* system : m_system_manager->getSystems()) {
			system->shutdownStarted();
		}

		m_prefab_resource_manager.destroy();

		m_system_manager.reset();
		m_input_system.reset();
		m_file_system.reset();

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

	void logToFile(LogLevel level, const char* message) {
		if (!m_is_log_file_open) return;
		bool success = true;
		if (level == LogLevel::ERROR) {
			success = m_log_file.write("Error: ", stringLength("Error :"));
		}
		success = m_log_file.write(message, stringLength(message)) && success ;
		success = m_log_file.write("\n", 1) && success ;
		ASSERT(success);
		if (level == LogLevel::ERROR) {
			m_log_file.flush();
		}
	}

	os::WindowHandle getMainWindow() override { return m_window_handle; }
	IAllocator& getAllocator() override { return m_allocator; }
	PageAllocator& getPageAllocator() override { return m_page_allocator; }

	EntityPtr instantiatePrefab(World& world,
		const struct PrefabResource& prefab,
		const struct DVec3& pos,
		const struct Quat& rot,
		const Vec3& scale,
		EntityMap& entity_map) override
	{
		ASSERT(prefab.isReady());
		InputMemoryStream blob(prefab.data);
		WorldVersion editor_header_version;
		if (!world.deserialize(blob, entity_map, editor_header_version)) {
			logError("Failed to instantiate prefab ", prefab.getPath());
			return INVALID_ENTITY;
		}

		ASSERT(!entity_map.m_map.empty());
		const EntityRef root = (EntityRef)entity_map.m_map[0];
		ASSERT(!world.getParent(root).isValid());
		ASSERT(!world.getNextSibling(root).isValid());
		world.setTransform(root, pos, rot, scale);
		return root;
	}

	World& createWorld() override {
		return *LUMIX_NEW(m_allocator, World)(*this);
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

	bool decompress(Span<const u8> src, Span<u8> output) override {
		const i32 result = LZ4_decompress_safe((const char*)src.begin(), (char*)output.begin(), src.length(), output.length());
		return result == output.length();
	}

	bool compress(Span<const u8> mem, OutputMemoryStream& output) override {
		const i32 cap = LZ4_compressBound(mem.length());
		const u32 start_size = (u32)output.size();
		output.resize(cap + start_size);
		jobs::MutexGuard guard(m_lz4_mutex);
		const i32 compressed_size = LZ4_compress_fast_extState(m_lz4_state, (const char*)mem.begin(), (char*)output.getMutableData() + start_size, mem.length(), cap, 1); 
		if (compressed_size == 0) return false;
		output.resize(compressed_size + start_size);
		return true;
	}

	void setTimeMultiplier(float multiplier) override
	{
		m_time_multiplier = maximum(multiplier, 0.001f);
	}

	void computeSmoothTimeDelta() {
		float tmp[11];
		memcpy(tmp, m_last_time_deltas, sizeof(tmp));
		sort(tmp, tmp + lengthOf(tmp), [](float a, float b) { return a < b; });
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
		{
			PROFILE_BLOCK("end frame");
			for (UniquePtr<IModule>& module : world.getModules()) {
				module->endFrame();
			}
		}

		PROFILE_FUNCTION();
		static u32 mem_counter = profiler::createCounter("Main allocator (MB)", 0);
		profiler::pushCounter(mem_counter, float(double(debug::getRegisteredAllocsSize()) / (1024.0 * 1024.0)));

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
			auto& modules =  world.getModules();
			jobs::forEach(modules.size(), 1, [&](u32 idx, u32){
				modules[idx]->updateParallel(dt);
			});
			{
				PROFILE_BLOCK("update modules");
				for (UniquePtr<IModule>& module : modules)
				{
					module->update(dt);
				}
			}
			{
				PROFILE_BLOCK("late update modules");
				for (UniquePtr<IModule>& module : modules)
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

	DeserializeProjectResult deserializeProject(InputMemoryStream& serializer, Path& startup_world) override {
		ProjectHeader header;
		serializer.read(header);
		if (header.magic != SERIALIZED_PROJECT_MAGIC) return DeserializeProjectResult::CORRUPTED_FILE;
		if (header.version > ProjectVersion::LAST) return DeserializeProjectResult::VERSION_NOT_SUPPORTED;
		if (header.version <= ProjectVersion::HASH64) return DeserializeProjectResult::VERSION_NOT_SUPPORTED;
		startup_world = serializer.readString();
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

	void serializeProject(OutputMemoryStream& serializer, const Path& startup_world) const override {
		ProjectHeader header;
		header.magic = SERIALIZED_PROJECT_MAGIC;
		header.version = ProjectVersion::LAST;
		serializer.write(header);
		serializer.writeString(startup_world.c_str());
		const Array<ISystem*>& systems = m_system_manager->getSystems();
		serializer.write((i32)systems.size());
		for (ISystem* system : systems) {
			const StableHash hash(system->getName());
			serializer.write(hash);
			serializer.write(system->getVersion());
			system->serialize(serializer);
		}
	}

	SystemManager& getSystemManager() override { return *m_system_manager; }
	FileSystem& getFileSystem() override { return *m_file_system; }
	InputSystem& getInputSystem() override { return *m_input_system; }
	ResourceManagerHub& getResourceManager() override { return m_resource_manager; }
	float getLastTimeDelta() const override { return m_smooth_time_delta / m_time_multiplier; }

private:
	TagAllocator m_allocator;
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
	os::WindowHandle m_window_handle = os::INVALID_WINDOW;
	os::OutputFile m_log_file;
	bool m_is_log_file_open = false;
	u8* m_lz4_state = nullptr;
	jobs::Mutex m_lz4_mutex;
};


UniquePtr<Engine> Engine::create(InitArgs&& init_data, IAllocator& allocator)
{
	return UniquePtr<EngineImpl>::create(allocator, static_cast<InitArgs&&>(init_data), allocator);
}


} // namespace Lumix
