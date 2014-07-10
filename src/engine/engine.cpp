#include "core/lumix.h"
#include "engine/engine.h"

#include "animation/animation.h"
#include "animation/animation_system.h"

#include "core/crc32.h"
#include "core/input_system.h"
#include "core/log.h"
#include "core/resource_manager.h"
#include "core/timer.h"

#include "core/fs/disk_file_device.h"
#include "core/fs/file_system.h"
#include "core/fs/memory_file_device.h"

#include "core/mtjd/manager.h"

#include "engine/plugin_manager.h"

#include "graphics/culling_system.h"
#include "graphics/material_manager.h"
#include "graphics/model_manager.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/shader_manager.h"
#include "graphics/texture_manager.h"

#include "script/script_system.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace Lumix
{

	struct EngineImpl
	{
		EngineImpl(Engine& engine) : m_owner(engine), m_script_system(ScriptSystem::create()) {}
		~EngineImpl();
		bool create(const char* base_path, Engine& owner);

		Renderer* m_renderer;
		FS::FileSystem* m_file_system; 
		FS::MemoryFileDevice* m_mem_file_device;
		FS::DiskFileDevice* m_disk_file_device;

		ResourceManager m_resource_manager;
		MaterialManager m_material_manager;
		ModelManager	m_model_manager;
		ShaderManager	m_shader_manager;
		TextureManager	m_texture_manager;
		PipelineManager m_pipeline_manager;
		AnimationManager m_animation_manager;
		MTJD::Manager	m_mtjd_manager;
		CullingSystem*	m_culling_system;

		string m_base_path;
		EditorServer* m_editor_server;
		PluginManager m_plugin_manager;
		Universe* m_universe;
		RenderScene* m_render_scene;
		ScriptSystem* m_script_system;
		InputSystem m_input_system;
		Engine& m_owner;
		Timer* m_timer;
		Timer* m_fps_timer;
		int	m_fps_frame;
		float m_fps;

		private:
			void operator=(const EngineImpl&);
	};


	void showLogInVS(const char*, const char* message)
	{
		OutputDebugString(message);
		OutputDebugString("\n");
	}


	EngineImpl::~EngineImpl()
	{
		ScriptSystem::destroy(m_script_system);
		m_resource_manager.get(ResourceManager::TEXTURE)->releaseAll();
		m_resource_manager.get(ResourceManager::MATERIAL)->releaseAll();
		m_resource_manager.get(ResourceManager::SHADER)->releaseAll();
		m_resource_manager.get(ResourceManager::ANIMATION)->releaseAll();
		m_resource_manager.get(ResourceManager::MODEL)->releaseAll();
		m_resource_manager.get(ResourceManager::PIPELINE)->releaseAll();
	}


	bool EngineImpl::create(const char* base_path, Engine& owner)
	{
		m_timer = Timer::create();
		m_fps_timer = Timer::create();
		m_fps_frame = 0;
		m_universe = 0;
		m_base_path = base_path;
		m_render_scene = NULL;

		m_renderer = Renderer::createInstance();
		if(!m_renderer)
		{
			return false;
		}
		if(!m_renderer->create(owner))
		{
			Renderer::destroyInstance(*m_renderer);
			return false;
		}
		if(!m_plugin_manager.create(owner))
		{
			return false;
		}
		AnimationSystem* anim_system = LUMIX_NEW(AnimationSystem)();
		if(!anim_system->create(owner))
		{
			LUMIX_DELETE(anim_system);
			return false;
		}
		m_plugin_manager.addPlugin(anim_system);
		if(!m_input_system.create())
		{
			return false;
		}
		m_script_system->setEngine(m_owner);
		return true;
	}

	
	bool Engine::create(const char* base_path, FS::FileSystem* file_system, EditorServer* editor_server)
	{
		g_log_info.getCallback().bind<showLogInVS>();
		g_log_warning.getCallback().bind<showLogInVS>();
		g_log_error.getCallback().bind<showLogInVS>();

		m_impl = LUMIX_NEW(EngineImpl)(*this);
		m_impl->m_editor_server = editor_server;

		if(NULL == file_system)
		{
			m_impl->m_file_system = FS::FileSystem::create();

			m_impl->m_mem_file_device = LUMIX_NEW(FS::MemoryFileDevice);
			m_impl->m_disk_file_device = LUMIX_NEW(FS::DiskFileDevice);

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

		if(!m_impl->create(base_path, *this))
		{
			LUMIX_DELETE(m_impl);
			m_impl = NULL;
			return false;
		}

		m_impl->m_resource_manager.create(*m_impl->m_file_system);
		m_impl->m_material_manager.create(ResourceManager::MATERIAL, m_impl->m_resource_manager);
		m_impl->m_model_manager.create(ResourceManager::MODEL, m_impl->m_resource_manager);
		m_impl->m_shader_manager.create(ResourceManager::SHADER, m_impl->m_resource_manager);
		m_impl->m_texture_manager.create(ResourceManager::TEXTURE, m_impl->m_resource_manager);
		m_impl->m_pipeline_manager.create(ResourceManager::PIPELINE, m_impl->m_resource_manager);
		m_impl->m_animation_manager.create(ResourceManager::ANIMATION, m_impl->m_resource_manager);
		m_impl->m_culling_system = CullingSystem::create(m_impl->m_mtjd_manager);

		return true;
	}


	void Engine::destroy()
	{
		Timer::destroy(m_impl->m_timer);
		Timer::destroy(m_impl->m_fps_timer);
		m_impl->m_plugin_manager.destroy();
		Renderer::destroyInstance(*m_impl->m_renderer);
		CullingSystem::destroy(*m_impl->m_culling_system);

		m_impl->m_input_system.destroy();
		m_impl->m_material_manager.destroy();
		
		if(m_impl->m_disk_file_device)
		{
			FS::FileSystem::destroy(m_impl->m_file_system);
			LUMIX_DELETE(m_impl->m_mem_file_device);
			LUMIX_DELETE(m_impl->m_disk_file_device);
		}

		LUMIX_DELETE(m_impl);
		m_impl = 0;
	}

	Universe* Engine::createUniverse()
	{
		m_impl->m_universe = LUMIX_NEW(Universe)();
		m_impl->m_render_scene = RenderScene::createInstance(*this, *m_impl->m_universe);
		m_impl->m_plugin_manager.onCreateUniverse(*m_impl->m_universe);
		m_impl->m_script_system->setUniverse(m_impl->m_universe);
		m_impl->m_universe->create();
		
		return m_impl->m_universe;
	}

	void Engine::destroyUniverse()
	{
		ASSERT(m_impl->m_universe);
		if (m_impl->m_universe)
		{
			m_impl->m_script_system->setUniverse(NULL);
			m_impl->m_plugin_manager.onDestroyUniverse(*m_impl->m_universe);
			m_impl->m_universe->destroy();
			RenderScene::destroyInstance(m_impl->m_render_scene);
			m_impl->m_render_scene = NULL;
			LUMIX_DELETE(m_impl->m_universe);
			m_impl->m_universe = 0;
		}
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
		return *m_impl->m_renderer;
	}


	void Engine::update()
	{
		++m_impl->m_fps_frame;
		if(m_impl->m_fps_frame == 30)
		{
			m_impl->m_fps = 30.0f / m_impl->m_fps_timer->tick();
			m_impl->m_fps_frame = 0;
		}
		float dt = m_impl->m_timer->tick();
		m_impl->m_render_scene->update(dt);
		m_impl->m_script_system->update(dt);
		m_impl->m_plugin_manager.update(dt);
		m_impl->m_input_system.update(dt);
	}


	IPlugin* Engine::loadPlugin(const char* name)
	{
		return m_impl->m_plugin_manager.load(name);
	}

	

	ScriptSystem& Engine::getScriptSystem()
	{
		return *m_impl->m_script_system;
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


	RenderScene* Engine::getRenderScene() const
	{
		return m_impl->m_render_scene;
	}


	MTJD::Manager& Engine::getMTJDManager() const
	{
		return m_impl->m_mtjd_manager;
	}


	CullingSystem& Engine::getCullingSystem() const
	{
		return *m_impl->m_culling_system;
	}


	ResourceManager& Engine::getResourceManager() const
	{
		return m_impl->m_resource_manager;
	}

	float Engine::getFPS() const
	{
		return m_impl->m_fps;
	}


	void Engine::serialize(ISerializer& serializer)
	{
		m_impl->m_universe->serialize(serializer);
		m_impl->m_renderer->serialize(serializer);
		m_impl->m_render_scene->serialize(serializer);
		m_impl->m_script_system->serialize(serializer);
		m_impl->m_plugin_manager.serialize(serializer);
	}


	void Engine::deserialize(ISerializer& serializer)
	{
		m_impl->m_universe->deserialize(serializer);
		m_impl->m_renderer->deserialize(serializer);
		m_impl->m_render_scene->deserialize(serializer);
		m_impl->m_script_system->deserialize(serializer);
		m_impl->m_plugin_manager.deserialize(serializer);
	}


} // ~namespace Lumix
