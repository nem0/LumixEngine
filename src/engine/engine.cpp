#include "lumix.h"
#include "engine.h"
#include "core/blob.h"
#include "core/crc32.h"
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
#include "debug/allocator.h"
#include "debug/debug.h"
#include "engine/iplugin.h"
#include "plugin_manager.h"
#include "universe/hierarchy.h"
#include "universe/universe.h"

#include <cstdio>

namespace Lumix
{

static const uint32 SERIALIZED_ENGINE_MAGIC = 0x5f4c454e; // == '_LEN'


enum class SerializedEngineVersion : int32
{
	BASE,
	SPARSE_TRANFORMATIONS,
	FOG_PARAMS,
	SCENE_VERSION,

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
		, m_mtjd_manager(m_allocator)
		, m_fps(0)
		, m_is_game_running(false)
		, m_component_types(m_allocator)
	{
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
		if (!m_input_system.create(m_allocator))
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
		m_input_system.destroy();
		if (m_disk_file_device)
		{
			FS::FileSystem::destroy(m_file_system);
			LUMIX_DELETE(m_allocator, m_mem_file_device);
			LUMIX_DELETE(m_allocator, m_disk_file_device);
		}

		m_resource_manager.destroy();
	}


	IAllocator& getAllocator() override { return m_allocator; }


	UniverseContext& createUniverse() override
	{
		UniverseContext* context = LUMIX_NEW(m_allocator, UniverseContext)(m_allocator);
		context->m_universe = LUMIX_NEW(m_allocator, Universe)(m_allocator);
		context->m_hierarchy =
			Hierarchy::create(*context->m_universe, m_allocator);
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


	MTJD::Manager& getMTJDManager() override { return m_mtjd_manager; }


	void destroyUniverse(UniverseContext& context) override
	{
		for (int i = context.m_scenes.size() - 1; i >= 0; --i)
		{
			context.m_scenes[i]->getPlugin().destroyScene(context.m_scenes[i]);
		}
		Hierarchy::destroy(context.m_hierarchy);
		LUMIX_DELETE(m_allocator, context.m_universe);

		LUMIX_DELETE(m_allocator, &context);
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
		dt = m_timer->tick();
		m_last_time_delta = dt;
		for (int i = 0; i < context.m_scenes.size(); ++i)
		{
			context.m_scenes[i]->update(dt);
		}
		m_plugin_manager->update(dt);
		m_input_system.update(dt);
		getFileSystem().updateAsyncTransactions();
	}


	InputSystem& getInputSystem() override { return m_input_system; }


	ResourceManager& getResourceManager() override
	{
		return m_resource_manager;
	}


	float getFPS() const override { return m_fps; }


	void serializePluginList(OutputBlob& serializer)
	{
		serializer.write((int32)m_plugin_manager->getPlugins().size());
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			serializer.writeString(plugin->getName());
		}
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
		g_path_manager.serialize(serializer);
		int pos = serializer.getSize();
		ctx.m_universe->serialize(serializer);
		ctx.m_hierarchy->serialize(serializer);
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


	bool deserialize(UniverseContext& ctx,
							 InputBlob& serializer) override
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
		g_path_manager.deserialize(serializer);
		ctx.m_universe->deserialize(serializer);
		ctx.m_hierarchy->deserialize(serializer);
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
		g_path_manager.clear();
		return true;
	}


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
	
	MTJD::Manager m_mtjd_manager;

	Array<ComponentType> m_component_types;
	PluginManager* m_plugin_manager;
	InputSystem m_input_system;
	Timer* m_timer;
	Timer* m_fps_timer;
	int m_fps_frame;
	float m_fps;
	float m_last_time_delta;
	bool m_is_game_running;

private:
	void operator=(const EngineImpl&);
	EngineImpl(const EngineImpl&);
};


static void showLogInVS(const char*, const char* message)
{
	Debug::debugOutput(message);
	Debug::debugOutput("\n");
}


static FILE* g_error_file = nullptr;


static void logErrorToFile(const char*, const char* message)
{
	fputs(message, g_error_file);
	fflush(g_error_file);
}


Engine* Engine::create(FS::FileSystem* fs, IAllocator& allocator)
{
	installUnhandledExceptionHandler();

	g_error_file = fopen("error.log", "wb");

	g_log_error.getCallback().bind<logErrorToFile>();
	g_log_info.getCallback().bind<showLogInVS>();
	g_log_warning.getCallback().bind<showLogInVS>();
	g_log_error.getCallback().bind<showLogInVS>();

	EngineImpl* engine = LUMIX_NEW(allocator, EngineImpl)(fs, allocator);
	if (!engine->create())
	{
		LUMIX_DELETE(allocator, engine);
		return nullptr;
	}
	return engine;
}


void Engine::destroy(Engine* engine, IAllocator& allocator)
{
	LUMIX_DELETE(allocator, engine);

	fclose(g_error_file);
	g_error_file = nullptr;
}


} // ~namespace Lumix
