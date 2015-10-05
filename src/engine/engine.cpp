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
#include "engine/property_descriptor.h"
#include "plugin_manager.h"
#include "universe/hierarchy.h"
#include "universe/universe.h"

#include <cstdio>

namespace Lumix
{

static const uint32_t SERIALIZED_ENGINE_MAGIC = 0x5f4c454e; // == '_LEN'


enum class SerializedEngineVersion : int32_t
{
	BASE,
	SPARSE_TRANFORMATIONS,

	LATEST // must be the last one
};


#pragma pack(1)
class SerializedEngineHeader
{
public:
	uint32_t m_magic;
	SerializedEngineVersion m_version;
	uint32_t m_reserved; // for crc
};
#pragma pack()


IScene* UniverseContext::getScene(uint32_t hash) const
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
		, m_component_properties(m_allocator)
		, m_component_types(m_allocator)
	{
		if (!fs)
		{
			m_file_system = FS::FileSystem::create(m_allocator);

			m_mem_file_device =
				m_allocator.newObject<FS::MemoryFileDevice>(m_allocator);
			m_disk_file_device =
				m_allocator.newObject<FS::DiskFileDevice>(m_allocator);

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


	virtual ~EngineImpl()
	{
		for (int j = 0; j < m_component_properties.size(); ++j)
		{
			Array<IPropertyDescriptor*>& props = m_component_properties.at(j);
			for (int i = 0, c = props.size(); i < c; ++i)
			{
				m_allocator.deleteObject(props[i]);
			}
		}

		Timer::destroy(m_timer);
		Timer::destroy(m_fps_timer);
		PluginManager::destroy(m_plugin_manager);
		m_input_system.destroy();
		if (m_disk_file_device)
		{
			FS::FileSystem::destroy(m_file_system);
			m_allocator.deleteObject(m_mem_file_device);
			m_allocator.deleteObject(m_disk_file_device);
		}

		m_resource_manager.destroy();
	}


	virtual IAllocator& getAllocator() override { return m_allocator; }


	virtual const char* getComponentTypeName(int index) override
	{
		return m_component_types[index].m_name.c_str();
	}


	virtual const char* getComponentTypeID(int index) override
	{
		return m_component_types[index].m_id.c_str();
	}


	virtual int getComponentTypesCount() const override
	{
		return m_component_types.size();
	}


	Array<IPropertyDescriptor*>& getPropertyDescriptors(uint32_t type)
	{
		int props_index = m_component_properties.find(type);
		if (props_index < 0)
		{
			m_component_properties.insert(
				type, Array<IPropertyDescriptor*>(m_allocator));
			props_index = m_component_properties.find(type);
		}
		return m_component_properties.at(props_index);
	}


	virtual const IPropertyDescriptor&
	getPropertyDescriptor(uint32_t type, uint32_t name_hash) override
	{
		Array<IPropertyDescriptor*>& props = getPropertyDescriptors(type);
		for (int i = 0; i < props.size(); ++i)
		{
			if (props[i]->getNameHash() == name_hash)
			{
				return *props[i];
			}
		}
		ASSERT(false);
		return *props[0];
	}


	virtual IPropertyDescriptor* getProperty(const char* component_type,
		const char* property_name) override
	{
		auto& props = getPropertyDescriptors(crc32(component_type));
		auto name_hash = crc32(property_name);
		for (int i = 0; i < props.size(); ++i)
		{
			if (props[i]->getNameHash() == name_hash)
			{
				return props[i];
			}
		}
		return nullptr;
	}


	virtual void registerComponentType(const char* id, const char* name) override
	{
		ComponentType& type = m_component_types.emplace(m_allocator);
		type.m_name = name;
		type.m_id = id;
	}


	virtual void registerProperty(const char* component_type,
								  IPropertyDescriptor* descriptor) override
	{
		ASSERT(descriptor);
		getPropertyDescriptors(crc32(component_type)).push(descriptor);
	}


	virtual UniverseContext& createUniverse() override
	{
		UniverseContext* context =
			m_allocator.newObject<UniverseContext>(m_allocator);
		context->m_universe = m_allocator.newObject<Universe>(m_allocator);
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


	virtual MTJD::Manager& getMTJDManager() override { return m_mtjd_manager; }


	virtual void destroyUniverse(UniverseContext& context) override
	{
		for (int i = context.m_scenes.size() - 1; i >= 0; --i)
		{
			context.m_scenes[i]->getPlugin().destroyScene(context.m_scenes[i]);
		}
		Hierarchy::destroy(context.m_hierarchy);
		m_allocator.deleteObject(context.m_universe);

		m_allocator.deleteObject(&context);
	}


	virtual PluginManager& getPluginManager() override
	{
		return *m_plugin_manager;
	}


	virtual FS::FileSystem& getFileSystem() override { return *m_file_system; }


	virtual void startGame(UniverseContext& context) override
	{
		ASSERT(!m_is_game_running);
		m_is_game_running = true;
		for (auto* scene : context.m_scenes)
		{
			scene->startGame();
		}
	}


	virtual void stopGame(UniverseContext& context) override
	{
		ASSERT(m_is_game_running);
		m_is_game_running = false;
		for (auto* scene : context.m_scenes)
		{
			scene->stopGame();
		}
	}


	virtual void update(UniverseContext& context) override
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


	virtual IPlugin* loadPlugin(const char* name) override
	{
		return m_plugin_manager->load(name);
	}


	virtual InputSystem& getInputSystem() override { return m_input_system; }


	virtual ResourceManager& getResourceManager() override
	{
		return m_resource_manager;
	}


	virtual float getFPS() const override { return m_fps; }


	void serializePluginList(OutputBlob& serializer)
	{
		serializer.write((int32_t)m_plugin_manager->getPlugins().size());
		for (auto* plugin : m_plugin_manager->getPlugins())
		{
			serializer.writeString(plugin->getName());
		}
	}


	bool hasSerializedPlugins(InputBlob& serializer)
	{
		int32_t count;
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


	virtual uint32_t serialize(UniverseContext& ctx,
							   OutputBlob& serializer) override
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
		serializer.write((int32_t)ctx.m_scenes.size());
		for (int i = 0; i < ctx.m_scenes.size(); ++i)
		{
			serializer.writeString(ctx.m_scenes[i]->getPlugin().getName());
			ctx.m_scenes[i]->serialize(serializer);
		}
		uint32_t crc = crc32((const uint8_t*)serializer.getData() + pos,
							 serializer.getSize() - pos);
		return crc;
	}


	virtual bool deserialize(UniverseContext& ctx,
							 InputBlob& serializer) override
	{
		SerializedEngineHeader header;
		serializer.read(header);
		if (header.m_magic != SERIALIZED_ENGINE_MAGIC)
		{
			g_log_error.log("engine") << "Wrong or corrupted file";
			return false;
		}
		if (header.m_version != SerializedEngineVersion::LATEST)
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
		int32_t scene_count;
		serializer.read(scene_count);
		for (int i = 0; i < scene_count; ++i)
		{
			char tmp[32];
			serializer.readString(tmp, sizeof(tmp));
			IScene* scene = ctx.getScene(crc32(tmp));
			scene->deserialize(serializer);
		}
		g_path_manager.clear();
		return true;
	}


	virtual float getLastTimeDelta() override { return m_last_time_delta; }

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
	};

private:
	Debug::Allocator m_allocator;

	FS::FileSystem* m_file_system;
	FS::MemoryFileDevice* m_mem_file_device;
	FS::DiskFileDevice* m_disk_file_device;

	ResourceManager m_resource_manager;
	
	MTJD::Manager m_mtjd_manager;

	AssociativeArray<uint32_t, Array<IPropertyDescriptor*>>
		m_component_properties;
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

	EngineImpl* engine = allocator.newObject<EngineImpl>(fs, allocator);
	if (!engine->create())
	{
		allocator.deleteObject(engine);
		return nullptr;
	}
	return engine;
}


void Engine::destroy(Engine* engine, IAllocator& allocator)
{
	allocator.deleteObject(engine);

	fclose(g_error_file);
}


} // ~namespace Lumix
