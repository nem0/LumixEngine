#include "engine/engine.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "animation/animation_system.h"
#include "engine/plugin_manager.h"
#include "graphics/renderer.h"
#include "core/input_system.h"
#include "script/script_system.h"

#include "core/file_system.h"
#include "core/disk_file_device.h"
#include "core/memory_file_device.h"

namespace Lux
{
	struct EngineImpl
	{
		EngineImpl(Engine& engine) : m_owner(engine) {}
		bool create(int w, int h, const char* base_path, Engine& owner);

		Renderer m_renderer;
		FS::FileSystem* m_file_system; 
		FS::MemoryFileDevice* m_mem_file_device;
		FS::DiskFileDevice* m_disk_file_device;

		string m_base_path;
		EditorServer* m_editor_server;
		PluginManager m_plugin_manager;
		Universe* m_universe;
		ScriptSystem m_script_system;
		InputSystem m_input_system;
		Engine& m_owner;
	};


	bool EngineImpl::create(int w, int h, const char* base_path, Engine& owner)
	{
		m_universe = 0;
		m_base_path = base_path;

		if(!m_renderer.create(m_file_system, w, h, base_path))
		{
			return false;
		}
		if(!m_plugin_manager.create(owner))
		{
			return false;
		}
		AnimationSystem* anim_system = LUX_NEW(AnimationSystem)();
		if(!anim_system->create(owner))
		{
			LUX_DELETE(anim_system);
			return false;
		}
		m_plugin_manager.addPlugin(anim_system);
		if(!m_input_system.create())
		{
			return false;
		}
		m_script_system.setEngine(m_owner);
		return true;
	}


	bool Engine::create(int w, int h, const char* base_path, FS::FileSystem* file_system, EditorServer* editor_server)
	{
		m_impl = LUX_NEW(EngineImpl)(*this);
		m_impl->m_editor_server = editor_server;

		if(NULL == file_system)
		{
			m_impl->m_file_system = FS::FileSystem::create();

			m_impl->m_mem_file_device = LUX_NEW(FS::MemoryFileDevice);
			m_impl->m_disk_file_device = LUX_NEW(FS::DiskFileDevice);

			m_impl->m_file_system->mount(m_impl->m_mem_file_device);
			m_impl->m_file_system->mount(m_impl->m_disk_file_device);
			m_impl->m_file_system->setDefaultDevice("memory:disk");
			m_impl->m_file_system->setSaveGameDevice("memory:disk");
		}
		else
		{
			m_impl->m_file_system = file_system;
			m_impl->m_mem_file_device = NULL;
			m_impl->m_disk_file_device = NULL;
		}

		if(!m_impl->create(w, h, base_path, *this))
		{
			LUX_DELETE(m_impl);
			m_impl = NULL;
			return false;
		}
		return true;
	}


	void Engine::destroy()
	{
		m_impl->m_plugin_manager.destroy();
		m_impl->m_renderer.destroy();
		
		if(m_impl->m_disk_file_device)
		{
			FS::FileSystem::destroy(m_impl->m_file_system);
			LUX_DELETE(m_impl->m_mem_file_device);
			LUX_DELETE(m_impl->m_disk_file_device);
		}

		LUX_DELETE(m_impl);
		m_impl = 0;
	}

	Universe* Engine::createUniverse()
	{
		m_impl->m_universe = LUX_NEW(Universe)();
		m_impl->m_plugin_manager.onCreateUniverse(*m_impl->m_universe);
		m_impl->m_script_system.setUniverse(m_impl->m_universe);
		m_impl->m_universe->create();
		m_impl->m_renderer.setUniverse(m_impl->m_universe);

		return m_impl->m_universe;
	}

	void Engine::destroyUniverse()
	{
		m_impl->m_renderer.setUniverse(0);
		m_impl->m_script_system.setUniverse(0);
		m_impl->m_plugin_manager.onDestroyUniverse(*m_impl->m_universe);
		m_impl->m_universe->destroy();
		LUX_DELETE(m_impl->m_universe);
		m_impl->m_universe = 0;
	}

	EditorServer* Engine::getEditorServer() const
	{
		return m_impl->m_editor_server;
	}


	PluginManager& Engine::getPluginManager()
	{
		return m_impl->m_plugin_manager;
	}


	FS::FileSystem& Engine::getFileSystem()
	{
		return *m_impl->m_file_system;
	}


	Renderer& Engine::getRenderer()
	{
		return m_impl->m_renderer;
	}


	void Engine::update()
	{
		static long last_tick = GetTickCount();
		long tick = GetTickCount();
		float dt = (tick - last_tick) / 1000.0f;
		last_tick = tick;
		m_impl->m_script_system.update(dt);
		m_impl->m_plugin_manager.update(dt);
		m_impl->m_input_system.update(dt);
	}


	IPlugin* Engine::loadPlugin(const char* name)
	{
		return m_impl->m_plugin_manager.load(name);
	}

	

	ScriptSystem& Engine::getScriptSystem()
	{
		return m_impl->m_script_system;
	}


	InputSystem& Engine::getInputSystem()
	{
		return m_impl->m_input_system;
	}


	const char* Engine::getBasePath() const
	{
		return m_impl->m_base_path.c_str();
	}


	Universe* Engine::getUniverse() const
	{
		return m_impl->m_universe;
	}


	void Engine::serialize(ISerializer& serializer)
	{
		m_impl->m_universe->serialize(serializer);
		m_impl->m_renderer.serialize(serializer);
		m_impl->m_script_system.serialize(serializer);
		m_impl->m_plugin_manager.serialize(serializer);
	}


	void Engine::deserialize(ISerializer& serializer)
	{
		m_impl->m_universe->deserialize(serializer);
		m_impl->m_renderer.deserialize(serializer);
		m_impl->m_script_system.deserialize(serializer);
		m_impl->m_plugin_manager.deserialize(serializer);
	}


} // ~namespace Lux