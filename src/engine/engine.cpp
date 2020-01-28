#include <imgui/imgui.h>

#include "engine/engine.h"
#include "engine/crc32.h"
#include "engine/debug.h"
#include "engine/file_system.h"
#include "engine/input_system.h"
#include "engine/iplugin.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math.h"
#include "engine/page_allocator.h"
#include "engine/path.h"
#include "engine/plugin_manager.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/universe/universe.h"


namespace Lumix
{

void registerEngineAPI(lua_State* L, Engine* engine);

static const u32 SERIALIZED_ENGINE_MAGIC = 0x5f4c454e; // == '_LEN'


static OS::OutputFile g_log_file;
static bool g_is_log_file_open = false;


#pragma pack(1)
class SerializedEngineHeader
{
public:
	u32 m_magic;
	u32 m_reserved; // for crc
};
#pragma pack()


static void showLogDebugOutput(LogLevel level, const char* system, const char* message)
{
	if(level == LogLevel::ERROR) {
		Debug::debugOutput("Error: ");
	}
	Debug::debugOutput(system);
	Debug::debugOutput(":: ");
	Debug::debugOutput(message);
	Debug::debugOutput("\n");
}


static void logToFile(LogLevel level, const char*, const char* message)
{
	if (!g_is_log_file_open) return;
	if (level == LogLevel::ERROR) {
		g_log_file.write("Error: ", stringLength("Error :"));
	}
	g_log_file.write(message, stringLength(message));
	g_log_file.write("\n", 1);
	g_log_file.flush();
}


struct PrefabResourceManager final : public ResourceManager
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


class EngineImpl final : public Engine
{
public:
	void operator=(const EngineImpl&) = delete;
	EngineImpl(const EngineImpl&) = delete;

	EngineImpl(const char* working_dir, IAllocator& allocator)
		: m_allocator(allocator)
		, m_prefab_resource_manager(m_allocator)
		, m_resource_manager(m_allocator)
		, m_lua_resources(m_allocator)
		, m_last_lua_resource_idx(-1)
		, m_is_game_running(false)
		, m_last_time_delta(0)
		, m_path_manager(PathManager::create(m_allocator))
		, m_time_multiplier(1.0f)
		, m_paused(false)
		, m_next_frame(false)
	{
		g_is_log_file_open = g_log_file.open("lumix.log");
		
		logInfo("Core") << "Creating engine...";
		Profiler::setThreadName("Worker");
		installUnhandledExceptionHandler();

		getLogCallback().bind<logToFile>();
		getLogCallback().bind<showLogDebugOutput>();

		OS::logVersion();

		m_platform_data = {};
		m_state = luaL_newstate();
		luaL_openlibs(m_state);

		registerEngineAPI(m_state, this);

		m_file_system = FileSystem::create(working_dir, m_allocator);

		m_resource_manager.init(*m_file_system);
		m_prefab_resource_manager.create(PrefabResource::TYPE, m_resource_manager);

		Reflection::init(m_allocator);

		m_plugin_manager = PluginManager::create(*this);
		m_input_system = InputSystem::create(*this);

		logInfo("Core") << "Engine created.";
	}


	~EngineImpl()
	{
		for (Resource* res : m_lua_resources)
		{
			res->getResourceManager().unload(*res);
		}

		Reflection::shutdown();
		PluginManager::destroy(m_plugin_manager);
		if (m_input_system) InputSystem::destroy(*m_input_system);
		FileSystem::destroy(m_file_system);

		m_prefab_resource_manager.destroy();
		lua_close(m_state);

		getLogCallback().unbind<logToFile>();
		g_log_file.close();
		PathManager::destroy(*m_path_manager);
	}


	void setPlatformData(const PlatformData& data) override
	{
		m_platform_data = data;
	}


	const PlatformData& getPlatformData() override
	{
		return m_platform_data;
	}



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
		InputMemoryStream blob(prefab.data.begin(), prefab.data.byte_size());
		if (!deserialize(universe, blob, entity_map)) {
			logError("Engine") << "Failed to instantiate prefab " << prefab.getPath();
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
		Universe* universe = LUMIX_NEW(m_allocator, Universe)(m_allocator);
		const Array<IPlugin*>& plugins = m_plugin_manager->getPlugins();
		for (auto* plugin : plugins) {
			plugin->createScenes(*universe);
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
		auto& scenes = universe.getScenes();
		for (int i = scenes.size() - 1; i >= 0; --i)
		{
			auto* scene = scenes[i];
			scenes.pop();
			scene->clear();
			scene->getPlugin().destroyScene(scene);
		}
		LUMIX_DELETE(m_allocator, &universe);
		m_resource_manager.removeUnreferenced();
	}


	void startGame(Universe& context) override
	{
		ASSERT(!m_is_game_running);
		m_is_game_running = true;
		for (auto* scene : context.getScenes())
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
		for (auto* scene : context.getScenes())
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
			for (auto* scene : context.getScenes())
			{
				scene->update(dt, m_paused);
			}
		}
		{
			PROFILE_BLOCK("late update scenes");
			for (auto* scene : context.getScenes())
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
		for (auto* scene : ctx.getScenes())
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


	bool hasSupportedSceneVersions(InputMemoryStream& serializer, Universe& ctx)
	{
		i32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			u32 hash;
			serializer.read(hash);
			auto* scene = ctx.getScene(hash);
			int version;
			serializer.read(version);
			if (version != scene->getVersion())
			{
				logError("Core") << "Plugin " << scene->getPlugin().getName() << " has incompatible version";
				return false;
			}
		}
		return true;
	}


	bool hasSerializedPlugins(InputMemoryStream& serializer)
	{
		i32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			char tmp[32];
			serializer.readString(Span(tmp));
			if (!m_plugin_manager->getPlugin(tmp))
			{
				logError("Core") << "Missing plugin " << tmp;
				return false;
			}
		}
		return true;
	}


	u32 serialize(Universe& ctx, OutputMemoryStream& serializer) override
	{
		SerializedEngineHeader header;
		header.m_magic = SERIALIZED_ENGINE_MAGIC; // == '_LEN'
		header.m_reserved = 0;
		serializer.write(header);
		serializePluginList(serializer);
		serializeSceneVersions(serializer, ctx);
		m_path_manager->serialize(serializer);
		int pos = (int)serializer.getPos();
		ctx.serialize(serializer);
		serializer.write((i32)ctx.getScenes().size());
		for (IScene* scene : ctx.getScenes())
		{
			serializer.writeString(scene->getPlugin().getName());
			scene->serialize(serializer);
		}
		u32 crc = crc32((const u8*)serializer.getData() + pos, (int)serializer.getPos() - pos);
		return crc;
	}


	bool deserialize(Universe& ctx, InputMemoryStream& serializer, Ref<EntityMap> entity_map) override
	{
		SerializedEngineHeader header;
		serializer.read(header);
		if (header.m_magic != SERIALIZED_ENGINE_MAGIC)
		{
			logError("Core") << "Wrong or corrupted file";
			return false;
		}
		if (!hasSerializedPlugins(serializer)) return false;
		if (!hasSupportedSceneVersions(serializer, ctx)) return false;

		m_path_manager->deserialize(serializer);
		ctx.deserialize(serializer, entity_map);
		i32 scene_count;
		serializer.read(scene_count);
		for (int i = 0; i < scene_count; ++i)
		{
			char tmp[32];
			serializer.readString(Span(tmp));
			IScene* scene = ctx.getScene(crc32(tmp));
			scene->deserialize(serializer, entity_map);
		}
		m_path_manager->clear();
		return true;
	}


	void unloadLuaResource(LuaResourceHandle resource) override
	{
		auto iter = m_lua_resources.find(resource);
		if (!iter.isValid()) return;
		Resource* res = iter.value();
		m_lua_resources.erase(iter);
		res->getResourceManager().unload(*res);
	}


	LuaResourceHandle addLuaResource(const Path& path, ResourceType type) override
	{
		Resource* res = m_resource_manager.load(type, path);
		if (!res) return 0xffFFffFF;
		++m_last_lua_resource_idx;
		ASSERT(m_last_lua_resource_idx != 0xffFFffFF);
		m_lua_resources.insert(m_last_lua_resource_idx, res);
		return {m_last_lua_resource_idx};
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
	PathManager& getPathManager() override{ return *m_path_manager; }
	lua_State* getState() override { return m_state; }
	float getLastTimeDelta() const override { return m_last_time_delta / m_time_multiplier; }

private:
	IAllocator& m_allocator;
	PageAllocator m_page_allocator;

	FileSystem* m_file_system;

	ResourceManagerHub m_resource_manager;
	
	PluginManager* m_plugin_manager;
	PrefabResourceManager m_prefab_resource_manager;
	InputSystem* m_input_system;
	OS::Timer m_timer;
	float m_time_multiplier;
	float m_last_time_delta;
	bool m_is_game_running;
	bool m_paused;
	bool m_next_frame;
	PlatformData m_platform_data;
	PathManager* m_path_manager;
	lua_State* m_state;
	HashMap<int, Resource*> m_lua_resources;
	u32 m_last_lua_resource_idx;
};


Engine* Engine::create(const char* working_dir, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, EngineImpl)(working_dir, allocator);
}


void Engine::destroy(Engine* engine, IAllocator& allocator)
{
	LUMIX_DELETE(allocator, engine);
}


} // namespace Lumix
