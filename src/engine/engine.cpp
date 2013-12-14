#include "engine/engine.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "animation/animation_system.h"
#include "engine/plugin_manager.h"
#include "graphics/renderer.h"
#include "platform/input_system.h"
#include "script/script_system.h"

#include "core/file_system.h"
#include "core/disk_file_system.h"
#include "core/memory_file_system.h"
#include "core/tcp_file_system.h"

#include "core/tcp_file_server.h"

namespace Lux
{
	struct EngineImpl
	{
		bool create(int w, int h, const char* base_path, Engine& owner);

		Renderer m_renderer;
		FS::FileSystem* m_file_system; 
		FS::MemoryFileSystem m_mem_filesystem;
		FS::DiskFileSystem m_disk_filesystem;
		FS::TCPFileSystem m_tcp_filesystem;

		string m_base_path;
		EditorServer* m_editor_server;
		PluginManager m_plugin_manager;
		Universe* m_universe;
		ScriptSystem m_script_system;
		InputSystem m_input_system;
	};


	bool EngineImpl::create(int w, int h, const char* base_path, Engine& owner)
	{
		m_universe = 0;
		m_file_system = FS::FileSystem::create();

		FS::TCPFileServer* server = new FS::TCPFileServer();
		server->start();

		m_tcp_filesystem.start("127.0.0.1", 10001);

		m_file_system->mount(&m_mem_filesystem);
		m_file_system->mount(&m_disk_filesystem);
		m_file_system->mount(&m_tcp_filesystem);

		if(!m_renderer.create(m_file_system, w, h, base_path))
		{
			return false;
		}
		if(!m_plugin_manager.create(owner))
		{
			return false;
		}
		AnimationSystem* anim_system = new AnimationSystem();
		if(!anim_system->create(owner))
		{
			delete anim_system;
			return false;
		}
		m_plugin_manager.addPlugin(anim_system);
		if(!m_input_system.create())
		{
			return false;
		}
		m_script_system.setRenderer(&m_renderer);			
		return true;
	}


	bool Engine::create(int w, int h, const char* base_path, EditorServer* editor_server)
	{
		m_impl = new EngineImpl();
		m_impl->m_editor_server = editor_server;
		if(!m_impl->create(w, h, base_path, *this))
		{
			delete m_impl;
			m_impl = 0;
			return false;
		}
		return true;
	}


	void Engine::destroy()
	{
		m_impl->m_plugin_manager.destroy();
		m_impl->m_renderer.destroy();
		
		m_impl->m_tcp_filesystem.stop();
		FS::FileSystem::destroy(m_impl->m_file_system);

		delete m_impl;
		m_impl = 0;
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


	void Engine::destroyUniverse()
	{
		m_impl->m_renderer.setUniverse(0);
		m_impl->m_script_system.setUniverse(0);
		m_impl->m_plugin_manager.onDestroyUniverse(*m_impl->m_universe);
		m_impl->m_universe->destroy();
		delete m_impl->m_universe;
		m_impl->m_universe = 0;
	}

	Universe* Engine::createUniverse()
	{
		m_impl->m_universe = new Universe();
		m_impl->m_plugin_manager.onCreateUniverse(*m_impl->m_universe);
		m_impl->m_script_system.setUniverse(m_impl->m_universe);
		m_impl->m_universe->create();
		m_impl->m_renderer.setUniverse(m_impl->m_universe);

		return m_impl->m_universe;
	}


	ScriptSystem& Engine::getScriptSystem()
	{
		return m_impl->m_script_system;
	}


	InputSystem& Engine::getInputSystem()
	{
		return m_impl->m_input_system;
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