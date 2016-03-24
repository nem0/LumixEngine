#include "render_scene.h"

#include "core/crc32.h"
#include "core/iallocator.h"
#include "model.h"
#include "universe/universe.h"
#include "engine/engine.h"
#include "ray_cast_model_hit.h"
#include "core/resource_manager.h"
#include "model_manager.h"
#include "renderer.h"
#include "core/profiler.h"
#include "pose.h"


namespace Lumix
{

static const uint32 RENDERABLE_MODEL = crc32("renderable_model");
static const uint32 CAMERA_HASH = crc32("camera");


enum class RenderSceneVersion : int32
{
	FIRST,

	LATEST,
	INVALID = -1,
};


struct Camera
{
	Entity m_entity;
	float m_fov;
	float m_aspect;
	float m_near;
	float m_far;
	float m_width;
	float m_height;
};


class RenderSceneImpl : public RenderScene
{
	class ModelLoadedCallback
	{
	public:
		ModelLoadedCallback(RenderSceneImpl& scene, Model* model)
			: m_scene(scene)
			, m_ref_count(0)
			, m_model(model)
		{
			m_model->getObserverCb().bind<ModelLoadedCallback, &ModelLoadedCallback::callback>(
				this);
		}

		~ModelLoadedCallback()
		{
			m_model->getObserverCb().unbind<ModelLoadedCallback, &ModelLoadedCallback::callback>(
				this);
		}

		void callback(Resource::State old_state, Resource::State new_state)
		{
			if (new_state == Resource::State::READY)
			{
				m_scene.modelLoaded(m_model);
			}
			else if (old_state == Resource::State::READY && new_state != Resource::State::READY)
			{
				m_scene.modelUnloaded(m_model);
			}
		}

		Model* m_model;
		int m_ref_count;
		RenderSceneImpl& m_scene;
	};

public:
	RenderSceneImpl(Renderer& renderer,
					Engine& engine,
					Universe& universe,
					IAllocator& allocator)
					: m_engine(engine)
					, m_universe(universe)
					, m_renderer(renderer)
					, m_allocator(allocator)
					, m_model_loaded_callbacks(m_allocator)
					, m_renderables(m_allocator)
					, m_cameras(m_allocator)
					, m_renderable_created(m_allocator)
					, m_renderable_destroyed(m_allocator)
					, m_is_game_running(false)
					, m_debug_triangles(allocator)
	{
		//m_universe.entityTransformed()
			//.bind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
		m_renderables.reserve(5000);
	}


	~RenderSceneImpl()
	{
		auto& rm = m_engine.getResourceManager();

		//m_universe.entityTransformed()
			//.unbind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);

		for (int i = 0; i < m_model_loaded_callbacks.size(); ++i)
			LUMIX_DELETE(m_allocator, m_model_loaded_callbacks[i]);

		for (auto& i : m_renderables)
		{
			if(i.entity != INVALID_ENTITY && i.model)
			{
				auto& manager = i.model->getResourceManager();
				manager.get(ResourceManager::MODEL)->unload(*i.model);
				LUMIX_DELETE(m_allocator, i.pose);
			}
		}
	}


	ComponentIndex createComponent(uint32 type, Entity entity) override
	{
		while(entity >= m_renderables.size())
		{
			auto& r = m_renderables.emplace();
			r.entity = INVALID_ENTITY;
			r.model = nullptr;
			r.pose = nullptr;
		}
		auto& r = m_renderables[entity];
		r.entity = entity;
		r.model = nullptr;
		r.pose = nullptr;
		//r.matrix = m_universe.getMatrix(entity);
		m_universe.addComponent(entity, RENDERABLE_MODEL, this, entity);
		m_renderable_created.invoke(m_renderables.size() - 1);
		return entity;
	}


	void destroyComponent(ComponentIndex component, uint32 type) override
	{
		m_renderable_destroyed.invoke(component);

		setModel(component, nullptr);
		Entity entity = m_renderables[component].entity;
		LUMIX_DELETE(m_allocator, m_renderables[component].pose);
		m_renderables[component].pose = nullptr;
		m_renderables[component].entity = INVALID_ENTITY;
		m_universe.destroyComponent(entity, RENDERABLE_MODEL, this, component);
	}


	void serialize(OutputBlob& serializer) override
	{

	}


	void deserialize(InputBlob& serializer, int version) override
	{

	}


	IPlugin& getPlugin() const override { return m_renderer; }


	void update(float time_delta, bool paused) override
	{
		TODO("implement");
	}


	bool ownComponentType(uint32 type) const override
	{
		return type == RENDERABLE_MODEL;
	}


	ComponentIndex getComponent(Entity entity, uint32 type) override
	{
		if(type == RENDERABLE_MODEL)
		{
			if(entity >= m_renderables.size()) return INVALID_COMPONENT;
			return m_renderables[entity].entity != INVALID_ENTITY ? entity : INVALID_COMPONENT;
		}

		return INVALID_COMPONENT;
	}


	Universe& getUniverse() override { return m_universe; }


	void startGame() override
	{
		m_is_game_running = true;
	}


	void stopGame() override
	{
		m_is_game_running = false;
	}


	int getVersion() const override { return (int)RenderSceneVersion::LATEST; }


	void sendMessage(uint32 type, void* message) override
	{
		TODO("implement");
	}


	ComponentIndex createCamera(Entity entity)
	{
		Camera& camera = m_cameras.emplace();
		camera.m_entity = entity;
		camera.m_fov = 60;
		camera.m_width = 800;
		camera.m_height = 600;
		camera.m_aspect = 800.0f / 600.0f;
		camera.m_near = 0.1f;
		camera.m_far = 10000.0f;
		m_universe.addComponent(entity, CAMERA_HASH, this, m_cameras.size() - 1);
		return m_cameras.size() - 1;
	}

	void destroyCamera(ComponentIndex component)
	{
		Entity entity = m_cameras[component].m_entity;
		m_universe.destroyComponent(entity, CAMERA_HASH, this, component);
		m_cameras.erase(component);
	}


	Renderable* getRenderables() override
	{
		return &m_renderables[0];
	}

	Renderable* getRenderable(ComponentIndex cmp) override
	{
		return &m_renderables[cmp];
	}


	Engine& getEngine() const override { return m_engine; }


	IAllocator& getAllocator() override { return m_allocator; }


	const Array<DebugTriangle>& getDebugTriangles() const override
	{
		return m_debug_triangles;
	}


	void modelLoaded(Model* model)
	{
		for (int i = 0, c = m_renderables.size(); i < c; ++i)
		{
			if (m_renderables[i].entity != INVALID_ENTITY && m_renderables[i].model == model)
			{
				auto& r = m_renderables[i];
				r.pose = LUMIX_NEW(m_allocator, Pose)();
				//model->getPose(*r.pose);
				//r.matrix = m_universe.getMatrix(r.entity);
			}
		}
	}


	void modelUnloaded(Model* model)
	{
		for(int i = 0, c = m_renderables.size(); i < c; ++i)
		{
			if(m_renderables[i].entity != INVALID_ENTITY && m_renderables[i].model == model)
			{
				auto& r = m_renderables[i];
				LUMIX_DELETE(m_allocator, r.pose);
				r.pose = nullptr;
			}
		}
	}


	ModelLoadedCallback* getModelLoadedCallback(Model* model)
	{
		for (int i = 0; i < m_model_loaded_callbacks.size(); ++i)
		{
			if (m_model_loaded_callbacks[i]->m_model == model)
			{
				return m_model_loaded_callbacks[i];
			}
		}
		ModelLoadedCallback* new_callback =
			LUMIX_NEW(m_allocator, ModelLoadedCallback)(*this, model);
		m_model_loaded_callbacks.push(new_callback);
		return new_callback;
	}


	void setModel(ComponentIndex component, Model* model)
	{
		ASSERT(m_renderables[component].entity != INVALID_ENTITY);

		Model* old_model = m_renderables[component].model;
		bool no_change = model == old_model && old_model;
		if (no_change)
		{
			old_model->getResourceManager()
				.get(ResourceManager::MODEL)->unload(*old_model);
			return;
		}

		if (old_model != nullptr)
		{
			auto& rm = old_model->getResourceManager();
			ModelLoadedCallback* callback = getModelLoadedCallback(old_model);
			--callback->m_ref_count;
			old_model->getResourceManager().get(ResourceManager::MODEL)->unload(*old_model);
		}

		m_renderables[component].model = model;
		LUMIX_DELETE(m_allocator, m_renderables[component].pose);
		m_renderables[component].pose = nullptr;
		if (model)
		{
			ModelLoadedCallback* callback = getModelLoadedCallback(model);
			++callback->m_ref_count;

			if (model->isReady())
			{
				modelLoaded(model);
			}
		}
	}


	DelegateList<void(ComponentIndex)>& renderableCreated() override
	{
		return m_renderable_created;
	}


	DelegateList<void(ComponentIndex)>& renderableDestroyed() override
	{
		return m_renderable_destroyed;
	}


	private:
		IAllocator& m_allocator;
		Array<ModelLoadedCallback*> m_model_loaded_callbacks;

		Array<Renderable> m_renderables;

		Array<Camera> m_cameras;

		Universe& m_universe;
		Renderer& m_renderer;
		Engine& m_engine;

		bool m_is_game_running;
		DelegateList<void(ComponentIndex)> m_renderable_created;
		DelegateList<void(ComponentIndex)> m_renderable_destroyed;

		Array<DebugTriangle> m_debug_triangles;
};


RenderScene* RenderScene::createInstance(Renderer& renderer,
										 Engine& engine,
										 Universe& universe,
										 IAllocator& allocator)
{
	return LUMIX_NEW(allocator, RenderSceneImpl)(
		renderer, engine, universe, allocator);
}


void RenderScene::destroyInstance(RenderScene* scene)
{
	LUMIX_DELETE(scene->getAllocator(), static_cast<RenderSceneImpl*>(scene));
}


} // namespace Lumix



