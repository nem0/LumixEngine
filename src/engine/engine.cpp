#include "lumix.h"
#include "engine.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/fs/os_file.h"
#include "core/input_system.h"
#include "core/log.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/timer.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/file_system.h"
#include "core/fs/memory_file_device.h"
#include "core/mtjd/manager.h"
#include "debug/debug.h"
#include "engine/iplugin.h"
#include "plugin_manager.h"
#include "universe/hierarchy.h"
#include "universe/universe.h"

#include <cstdio>

namespace Lumix
{

static const uint32 SERIALIZED_ENGINE_MAGIC = 0x5f4c454e; // == '_LEN'
static const uint32 HIERARCHY_HASH = crc32("hierarchy");


enum class SerializedEngineVersion : int32
{
	BASE,
	SPARSE_TRANFORMATIONS,
	FOG_PARAMS,
	SCENE_VERSION,
	HIERARCHY_COMPONENT,
	SCENE_VERSION_CHECK,

	LATEST // must be the last one
};


#pragma pack(1)
class SerializedEngineHeader
{
public:
	uint32 m_magic;
	SerializedEngineVersion m_version;
	uint32 m_reserved; // for crc
};
#pragma pack()


IScene* UniverseContext::getScene(uint32 hash) const
{
	for (auto* scene : m_scenes)
	{
		if (crc32(scene->getPlugin().getName()) == hash)
		{
			return scene;
		}
	}
	return nullptr;
}


class EngineImpl : public Engine
{
public:
	EngineImpl(FS::FileSystem* fs, IAllocator& allocator)
		: m_allocator(allocator)
		, m_resource_manager(m_allocator)
		, m_mtjd_manager(nullptr)
		, m_fps(0)
		, m_is_game_running(false)
		, m_component_types(m_allocator)
		, m_last_time_delta(0)
		, m_path_manager(m_allocator)
		, m_time_multiplier(1.0f)
		, m_paused(false)
		, m_next_frame(false)
	{
		m_mtjd_manager = MTJD::Manager::create(m_allocator);
		if (!fs)
		{
			m_file_system = FS::FileSystem::create(m_allocator);

			m_mem_file_device = LUMIX_NEW(m_allocator, FS::MemoryFileDevice)(m_allocator);
			m_disk_file_device = LUMIX_NEW(m_allocator, FS::DiskFileDevice)(m_allocator);

			m_file_system->mount(m_mem_file_device);
			m_file_system->mount(m_disk_file_device);
			m_file_system->setDefaultDevice("memory:disk");
			m_file_system->setSaveGameDevice("memory:disk");
		}
		else
		{
			m_file_system = fs;
			m_mem_file_device = nullptr;
			m_disk_file_device = nullptr;
		}

		m_resource_manager.create(*m_file_system);

		m_timer = Timer::create(m_allocator);
		m_fps_timer = Timer::create(m_allocator);
		m_fps_frame = 0;
	}

	bool create()
	{
		m_plugin_manager = PluginManager::create(*this);
		if (!m_plugin_manager)
		{
			return false;
		}
		
		HierarchyPlugin* hierarchy = LUMIX_NEW(m_allocator, HierarchyPlugin)(m_allocator);
		m_plugin_manager->addPlugin(hierarchy);
		
		m_input_system = InputSystem::create(m_allocator);
		if (!m_input_system)
		{
			return false;
		}

		return true;
	}


	~EngineImpl()
	{
		Timer::destroy(m_timer);
		Timer::destroy(m_fps_timer);
		PluginManager::destroy(m_plugin_manager);
		if (m_input_system) InputSystem::destroy(*m_input_system);
		if (m_disk_file_device)
		{
			FS::FileSystem::destroy(m_file_system);
			LUMIX_DELETE(m_allocator, m_mem_file_device);
			LUMIX_DELETE(m_allocator, m_disk_file_device);
		}

		m_resource_manager.destroy();
		MTJD::Manager::destroy(*m_mtjd_manager);
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


	UniverseContext& createUniverse() override
	{
		UniverseContext* context = LUMIX_NEW(m_allocator, UniverseContext)(m_allocator);
		context->m_universe = LUMIX_NEW(m_allocator, Universe)(m_allocator);
		const Array<IPlugin*>& plugins = m_plugin_manager->getPlugins();
		for (auto* plugin : plugins)
		{
			IScene* scene = plugin->createScene(*context);
			if (scene)
			{
				context->m_scenes.push(scene);
			}
		}

		return *context;
	}


	MTJD::Manager& getMTJDManager() override { return *m_mtjd_manager; }


	void destroyUniverse(UniverseContext& context) override
	{
		for (int i = context.m_scenes.size() - 1; i >= 0; --i)
		{
			context.m_scenes[i]->getPlugin().destroyScene(context.m_scenes[i]);
		}
		LUMIX_DELETE(m_allocator, context.m_universe);

		LUMIX_DELETE(m_allocator, &context);
		m_resource_manager.removeUnreferenced();
	}


	PluginManager& getPluginManager() override
	{
		return *m_plugin_manager;
	}


	FS::FileSystem& getFileSystem() override { return *m_file_system; }


	void startGame(UniverseContext& context) override
	{
		ASSERT(!m_is_game_running);
		m_is_game_running = true;
		for (auto* scene : context.m_scenes)
		{
			scene->startGame();
		}
	}


	void stopGame(UniverseContext& context) override
	{
		ASSERT(m_is_game_running);
		m_is_game_running = false;
		for (auto* scene : context.m_scenes)
		{
			scene->stopGame();
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
		m_time_multiplier = multiplier;
	}


	void update(UniverseContext& context) override
	{
		PROFILE_FUNCTION();
		float dt;
		++m_fps_frame;
		if (m_fps_timer->getTimeSinceTick() > 0.5f)
		{
			m_fps = m_fps_frame / m_fps_timer->tick();
			m_fps_frame = 0;
		}
		dt = m_timer->tick() * m_time_multiplier;
		if (m_next_frame)
		{
			m_paused = false;
			dt = 1 / 30.0f;
		}
		m_last_time_delta = dt;
		{
			PROFILE_BLOCK("update scenes");
			for (int i = 0; i < context.m_scenes.size(); ++i)
			{
				context.m_scenes[i]->update(dt, m_paused);
			}
		}
		m_plugin_manager->update(dt, m_paused);
		m_input_system->update(dt);
		getFileSystem().updateAsyncTransactions();

		if (m_next_frame)
		{
			m_paused = true;
			m_next_frame = false;
		}
	}


	InputSystem& getInputSystem() override { return *m_input_system; }


	ResourceManager& getResourceManager() override
	{
		return m_resource_manager;
	}


	float getFPS() const override { return m_fps; }


	void serializerSceneVersions(OutputBlob& serializer, UniverseContext& ctx)
	{
		serializer.write(ctx.m_scenes.size());
		for (auto* scene : ctx.m_scenes)
		{
			serializer.write(crc32(scene->getPlugin().getName()));
			serializer.write(scene->getVersion());
		}
	}


	void serializePluginList(OutputBlob& serializer)
	{
		serializer.write((int32)m_plugin_manager->getPlugins().size());
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			serializer.writeString(plugin->getName());
		}
	}


	bool hasSupportedSceneVersions(InputBlob& serializer, UniverseContext& ctx)
	{
		int32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			uint32 hash;
			serializer.read(hash);
			auto* scene = ctx.getScene(hash);
			int version;
			serializer.read(version);
			if (version > scene->getVersion())
			{
				g_log_error.log("engine") << "Plugin " << scene->getPlugin().getName() << " is too old";
				return false;
			}
		}
		return true;
	}


	bool hasSerializedPlugins(InputBlob& serializer)
	{
		int32 count;
		serializer.read(count);
		for (int i = 0; i < count; ++i)
		{
			char tmp[32];
			serializer.readString(tmp, sizeof(tmp));
			if (!m_plugin_manager->getPlugin(tmp))
			{
				g_log_error.log("engine") << "Missing plugin " << tmp;
				return false;
			}
		}
		return true;
	}


	uint32 serialize(UniverseContext& ctx, OutputBlob& serializer) override
	{
		SerializedEngineHeader header;
		header.m_magic = SERIALIZED_ENGINE_MAGIC; // == '_LEN'
		header.m_version = SerializedEngineVersion::LATEST;
		header.m_reserved = 0;
		serializer.write(header);
		serializePluginList(serializer);
		serializerSceneVersions(serializer, ctx);
		m_path_manager.serialize(serializer);
		int pos = serializer.getSize();
		ctx.m_universe->serialize(serializer);
		m_plugin_manager->serialize(serializer);
		serializer.write((int32)ctx.m_scenes.size());
		for (int i = 0; i < ctx.m_scenes.size(); ++i)
		{
			serializer.writeString(ctx.m_scenes[i]->getPlugin().getName());
			serializer.write(ctx.m_scenes[i]->getVersion());
			ctx.m_scenes[i]->serialize(serializer);
		}
		uint32 crc = crc32((const uint8*)serializer.getData() + pos,
							 serializer.getSize() - pos);
		return crc;
	}


	bool deserialize(UniverseContext& ctx, InputBlob& serializer) override
	{
		SerializedEngineHeader header;
		serializer.read(header);
		if (header.m_magic != SERIALIZED_ENGINE_MAGIC)
		{
			g_log_error.log("engine") << "Wrong or corrupted file";
			return false;
		}
		if (header.m_version > SerializedEngineVersion::LATEST)
		{
			g_log_error.log("engine") << "Unsupported version";
			return false;
		}
		if (!hasSerializedPlugins(serializer))
		{
			return false;
		}
		if (header.m_version > SerializedEngineVersion::SCENE_VERSION_CHECK &&
			!hasSupportedSceneVersions(serializer, ctx))
		{
			return false;
		}

		m_path_manager.deserialize(serializer);
		ctx.m_universe->deserialize(serializer);

		if (header.m_version <= SerializedEngineVersion::HIERARCHY_COMPONENT)
		{
			ctx.getScene(HIERARCHY_HASH)->deserialize(serializer, 0);
		}

		m_plugin_manager->deserialize(serializer);
		int32 scene_count;
		serializer.read(scene_count);
		for (int i = 0; i < scene_count; ++i)
		{
			char tmp[32];
			serializer.readString(tmp, sizeof(tmp));
			IScene* scene = ctx.getScene(crc32(tmp));
			int scene_version = -1;
			if (header.m_version > SerializedEngineVersion::SCENE_VERSION)
			{
				serializer.read(scene_version);
			}
			scene->deserialize(serializer, scene_version);
		}
		m_path_manager.clear();
		return true;
	}


	PathManager& getPathManager() override{ return m_path_manager; }
	float getLastTimeDelta() override { return m_last_time_delta; }

private:
	struct ComponentType
	{
		ComponentType(IAllocator& allocator)
			: m_name(allocator)
			, m_id(allocator)
		{
		}

		string m_name;
		string m_id;

		uint32 m_id_hash;
		uint32 m_dependency;
	};

private:
	Debug::Allocator m_allocator;

	FS::FileSystem* m_file_system;
	FS::MemoryFileDevice* m_mem_file_device;
	FS::DiskFileDevice* m_disk_file_device;

	ResourceManager m_resource_manager;
	
	MTJD::Manager* m_mtjd_manager;

	Array<ComponentType> m_component_types;
	PluginManager* m_plugin_manager;
	InputSystem* m_input_system;
	Timer* m_timer;
	Timer* m_fps_timer;
	int m_fps_frame;
	float m_time_multiplier;
	float m_fps;
	float m_last_time_delta;
	bool m_is_game_running;
	bool m_paused;
	bool m_next_frame;
	PlatformData m_platform_data;
	PathManager m_path_manager;

private:
	void operator=(const EngineImpl&);
	EngineImpl(const EngineImpl&);
};


static void showLogInVS(const char* system, const char* message)
{
	Debug::debugOutput(system);
	Debug::debugOutput(" : ");
	Debug::debugOutput(message);
	Debug::debugOutput("\n");
}


static FS::OsFile g_error_file;
static bool g_is_error_file_opened = false;


static void logErrorToFile(const char*, const char* message)
{
	if (!g_is_error_file_opened) return;
	g_error_file.write(message, stringLength(message));
	g_error_file.flush();
}


Engine* Engine::create(FS::FileSystem* fs, IAllocator& allocator)
{
	g_log_info.log("engine") << "Creating engine...";
	Profiler::setThreadName("Main");
	installUnhandledExceptionHandler();

	g_is_error_file_opened = g_error_file.open("error.log", FS::Mode::CREATE | FS::Mode::WRITE, allocator);

	g_log_error.getCallback().bind<logErrorToFile>();
	g_log_info.getCallback().bind<showLogInVS>();
	g_log_warning.getCallback().bind<showLogInVS>();
	g_log_error.getCallback().bind<showLogInVS>();

	EngineImpl* engine = LUMIX_NEW(allocator, EngineImpl)(fs, allocator);
	if (!engine->create())
	{
		g_log_error.log("engine") << "Failed to create engine.";
		LUMIX_DELETE(allocator, engine);
		return nullptr;
	}
	g_log_info.log("engine") << "Engine created.";
	return engine;
}


void Engine::destroy(Engine* engine, IAllocator& allocator)
{
	LUMIX_DELETE(allocator, engine);

	g_error_file.close();
}


} // ~namespace Lumix
