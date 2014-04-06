#include "graphics/renderer.h"
#include <Windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include "core/array.h"
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/json_serializer.h"
#include "core/math_utils.h"
#include "core/array.h"
#include "core/vec4.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "engine/engine.h"
#include "graphics/geometry.h"
#include "graphics/gl_ext.h"
#include "graphics/irender_device.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/pipeline.h"
#include "graphics/shader.h"
#include "graphics/texture.h"
#include "universe/component_event.h"
#include "universe/entity_moved_event.h"
#include "universe/universe.h"


namespace Lux
{


static const uint32_t renderable_hash = crc32("renderable");


struct Renderable
{
	ModelInstance* m_model;
	Entity m_entity;
	float m_scale;
	int64_t m_layer_mask;
};


struct Light
{
	enum Type
	{
		GLOBAL
	};

	Type m_type;
	Entity m_entity;
};


struct Camera
{
	Entity m_entity;
	bool m_is_active;
	float m_fov;
	float m_aspect;
	float m_near;
	float m_far;
	float m_width;
	float m_height;
};


struct RendererImpl : public Renderer
{

	RendererImpl()
	{
		m_universe = NULL;
	}

	virtual void applyCamera(Component camera_component) override
	{
		Camera* camera = m_cameras[camera_component.index];
		Matrix mtx;
		camera->m_entity.getMatrix(mtx);
		glViewport(0, 0, (GLsizei)camera->m_width, (GLsizei)camera->m_height);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(camera->m_fov, camera->m_aspect, camera->m_near, camera->m_far);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		Vec3 pos = mtx.getTranslation();
		Vec3 center = pos - mtx.getZVector();
		Vec3 up = mtx.getYVector();
		gluLookAt(pos.x, pos.y, pos.z, center.x, center.y, center.z, up.x, up.y, up.z);
	}


	virtual void setCameraSize(Component camera, int w, int h) override
	{
		m_cameras[camera.index]->m_width = (float)w;
		m_cameras[camera.index]->m_height = (float)h;
		m_cameras[camera.index]->m_aspect = w / (float)h;
	}


	virtual void render(IRenderDevice& device) override
	{
		// init
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);		

		// render
		device.getPipeline().render();

		// cleanup
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(0);
		for(int i = 0; i < 16; ++i)
		{
			glActiveTexture(GL_TEXTURE0 + i);
			glDisable(GL_TEXTURE_2D);
		}
		glActiveTexture(GL_TEXTURE0); 
	}


	void onEntityMoved(Event& evt)
	{
		EntityMovedEvent e = static_cast<EntityMovedEvent&>(evt);
		const Entity::ComponentList& cmps = e.entity.getComponents();
		for(int i = 0; i < cmps.size(); ++i)
		{
			if(cmps[i].type == renderable_hash)
			{
				m_renderables[cmps[i].index].m_model->setMatrix(e.entity.getMatrix());
				break;	
			}
		}
	}


	void clearScene()
	{
		m_lights.clear();
		for (int i = 0; i < m_cameras.size(); ++i)
		{
			LUX_DELETE(m_cameras[i]);
		}
		m_cameras.clear();
		for (int i = 0; i < m_renderables.size(); ++i)
		{
			LUX_DELETE(m_renderables[i].m_model);
		}
		m_renderables.clear();
	}


	virtual void setUniverse(Universe* universe) override
	{
		if(m_universe)
		{
			EventManager::Listener cb;
			cb.bind<RendererImpl, &RendererImpl::onEntityMoved>(this);
			m_universe->getEventManager()->removeListener(EntityMovedEvent::type, cb);
			clearScene();
		}
		m_universe = universe;
		if(m_universe)
		{
			m_universe->getEventManager()->addListener(EntityMovedEvent::type).bind<RendererImpl, &RendererImpl::onEntityMoved>(this);
		}
	}


	virtual bool create(Engine& engine) override
	{
		m_engine = &engine;
		loadGLExtensions();
		return true;
	}


	virtual void destroy() override
	{
	}


	virtual const char* getName() const override
	{
		return "renderer";
	}


	virtual Component createComponent(uint32_t type, const Entity& entity) override
	{
		if(type == crc32("light"))
		{
			Light& light = m_lights.pushEmpty();
			light.m_type = Light::GLOBAL;
			light.m_entity = entity;
		}
		else if(type == crc32("camera"))
		{
			Camera* camera = LUX_NEW(Camera);
			camera->m_is_active = false;
			camera->m_entity = entity;
			camera->m_fov = 60;
			camera->m_width = 800;
			camera->m_height = 600;
			camera->m_aspect = 800.0f / 600.0f;
			camera->m_near = 0.1f;
			camera->m_far = 100.0f;
			m_cameras.push(camera);
			Component cmp(entity, type, this, m_cameras.size() - 1);
			m_universe->getEventManager()->emitEvent(ComponentEvent(cmp));
			return Component(entity, type, this, m_cameras.size() - 1);
		}
		else if(type == crc32("renderable"))
		{
			Renderable& r = m_renderables.pushEmpty();
			r.m_entity = entity;
			r.m_model = NULL;
			r.m_scale = 1.0f;
			r.m_layer_mask = 1;
			Component cmp(entity, type, this, m_renderables.size() - 1);
			m_universe->getEventManager()->emitEvent(ComponentEvent(cmp));
			return Component(entity, type, this, m_renderables.size() - 1);
		}
		return Component::INVALID;
	}


	virtual Pose& getPose(Component cmp) 
	{
		return m_renderables[cmp.index].m_model->getPose();
	}


	virtual void getCameraFov(Component cmp, float& fov) override
	{
		fov = m_cameras[cmp.index]->m_fov;
	}


	virtual void getRenderablePath(Component cmp, string& path) override
	{
		if(m_renderables[cmp.index].m_model)
		{
			path = m_renderables[cmp.index].m_model->getModel().getPath();
		}
		else
		{
			path = "";
		}
	}


	virtual void setRenderableLayer(Component cmp, const int32_t& layer) override
	{
		m_renderables[cmp.index].m_layer_mask = ((int64_t)1 << (int64_t)layer);
	}


	virtual void setRenderableScale(Component cmp, const float& scale) override
	{
		m_renderables[cmp.index].m_scale = scale;
	}


	virtual void setRenderablePath(Component cmp, const string& path) override
	{
		LUX_DELETE(m_renderables[cmp.index].m_model);
		Renderable& r = m_renderables[cmp.index];
		Model* model = static_cast<Model*>(m_engine->getResourceManager().get(ResourceManager::MODEL)->load(path));
		r.m_model = LUX_NEW(ModelInstance)(*model);
		r.m_model->setMatrix(r.m_entity.getMatrix());
	}


	virtual void getRenderableInfos(Array<RenderableInfo>& infos, int64_t layer_mask) override
	{
		infos.reserve(m_renderables.size());
		for(int i = 0; i < m_renderables.size(); ++i)
		{
			if(m_renderables[i].m_model != NULL && (m_renderables[i].m_layer_mask & layer_mask) != 0)
			{
				RenderableInfo& info = infos.pushEmpty();
				info.m_scale = m_renderables[i].m_scale;
				info.m_model_instance = m_renderables[i].m_model;	
			}
		}
	}


	virtual void enableZTest(bool enable) override
	{
		if (enable)
		{
			glEnable(GL_DEPTH_TEST);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);
		}
	}


	virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir) override
	{
		RayCastModelHit hit;
		hit.m_is_hit = false;
		for(int i = 0; i < m_renderables.size(); ++i)
		{
			if(m_renderables[i].m_model)
			{
				const Vec3& pos = m_renderables[i].m_model->getMatrix().getTranslation();
				float radius = m_renderables[i].m_model->getModel().getBoundingRadius();
				float scale = m_renderables[i].m_scale;
				Vec3 intersection;
				if(Math::getRaySphereIntersection(pos, radius * scale, origin, dir, intersection))
				{
					RayCastModelHit new_hit = m_renderables[i].m_model->getModel().castRay(origin, dir, m_renderables[i].m_model->getMatrix(), scale);
					if(new_hit.m_is_hit && (!hit.m_is_hit || new_hit.m_t < hit.m_t))
					{
						new_hit.m_renderable = Component(m_renderables[i].m_entity, crc32("renderable"), this, i);
						hit = new_hit;
						hit.m_is_hit = true;
					}
				}
			}
		}
		return hit;
	}


	virtual Component getLight(int index) override
	{
		if (index >= m_lights.size())
		{
			return Component::INVALID;
		}
		return Component(m_lights[index].m_entity, crc32("light"), this, index);
	};


	virtual void getRay(Component camera, float x, float y, Vec3& origin, Vec3& dir) override
	{
		Camera* cam = m_cameras[camera.index];
		Vec3 camera_pos = m_cameras[camera.index]->m_entity.getPosition();
		float nx = 2 * (x / cam->m_width) - 1;
		float ny = 2 * ((cam->m_height - y) / cam->m_height) - 1;
		Matrix projection_matrix = getProjectionMatrix(camera);
		Matrix view_matrix = m_cameras[camera.index]->m_entity.getMatrix();
		view_matrix.inverse();
		Matrix inverted = (projection_matrix * view_matrix);
		inverted.inverse();
		Vec4 p0 = inverted * Vec4(nx, ny, -1, 1);
		Vec4 p1 = inverted * Vec4(nx, ny, 1, 1);
		p0.x /= p0.w; p0.y /= p0.w; p0.z /= p0.w;
		p1.x /= p1.w; p1.y /= p1.w; p1.z /= p1.w;
		origin = camera_pos;
		dir.x = p1.x - p0.x;
		dir.y = p1.y - p0.y;
		dir.z = p1.z - p0.z;
		dir.normalize();
	}


	Matrix getProjectionMatrix(Component cmp)
	{
		const Camera* camera = m_cameras[cmp.index];
		Matrix mtx;
		mtx = Matrix::IDENTITY;
		float f = 1 / tanf(Math::degreesToRadians(camera->m_fov) * 0.5f);
		mtx.m11 = f / camera->m_aspect;
		mtx.m22 = f;
		mtx.m33 = (camera->m_far + camera->m_near) / (camera->m_near - camera->m_far);
		mtx.m44 = 0;
		mtx.m43 = (2 * camera->m_far * camera->m_near) / (camera->m_near - camera->m_far);
		mtx.m34 = -1;

		return mtx;
	}


	Pipeline* getPipeline(const char* path)
	{
		for(int i = 0; i < m_pipelines.size(); ++i)
		{
			if(strcmp(m_pipelines[i]->getPath(), path) == 0)
			{
				return m_pipelines[i];
			}
		}
		Pipeline* pipeline = Pipeline::create(*this);
		m_pipelines.push(pipeline);
		pipeline->load(path, m_engine->getFileSystem());
		return pipeline;
	}

	virtual Engine& getEngine() override
	{
		return *m_engine;
	}


	virtual void renderModel(const Model& model, const Matrix& transform) override
	{
		glPushMatrix();
		glMultMatrixf(&transform.m11);
		for (int i = 0, c = model.getMeshCount(); i < c;  ++i)
		{
			const Mesh& mesh = model.getMesh(i);
			mesh.getMaterial()->apply(*this);
			model.getGeometry()->draw(mesh.getStart(), mesh.getCount(), *mesh.getMaterial()->getShader());
		}
		glPopMatrix();
	}


	virtual Model* getModel(const char* path) override
	{
		return static_cast<Model*>(m_engine->getResourceManager().get(ResourceManager::MODEL)->load(path));
	}


	virtual Pipeline* loadPipeline(const char* path) override
	{
		/// TODO pipeline manager
		Pipeline* pipeline = Pipeline::create(*this);
		pipeline->load(path, m_engine->getFileSystem());
		return pipeline;
	}


	virtual void serialize(ISerializer& serializer) override
	{
		serializer.serialize("camera_count", m_cameras.size());
		serializer.beginArray("cameras");
		for (int i = 0; i < m_cameras.size(); ++i)
		{
			serializer.serializeArrayItem(m_cameras[i]->m_far);
			serializer.serializeArrayItem(m_cameras[i]->m_near);
			serializer.serializeArrayItem(m_cameras[i]->m_fov);
			serializer.serializeArrayItem(m_cameras[i]->m_is_active);
			serializer.serializeArrayItem(m_cameras[i]->m_width);
			serializer.serializeArrayItem(m_cameras[i]->m_height);
			serializer.serializeArrayItem(m_cameras[i]->m_entity.index);
		}
		serializer.endArray();
		serializer.serialize("renderable_count", m_renderables.size());
		serializer.beginArray("renderables");
		for (int i = 0; i < m_renderables.size(); ++i)
		{
			serializer.serializeArrayItem(m_renderables[i].m_entity.index);
			serializer.serializeArrayItem(m_renderables[i].m_model->getModel().getPath());
			serializer.serializeArrayItem(m_renderables[i].m_scale);
			Matrix mtx = m_renderables[i].m_model->getMatrix();
			for (int j = 0; j < 16; ++j)
			{
				serializer.serializeArrayItem((&mtx.m11)[j]);
			}
		}
		serializer.endArray();
	}


	virtual void deserialize(ISerializer& serializer) override
	{
		clearScene();
		int size;
		serializer.deserialize("camera_count", size);
		serializer.deserializeArrayBegin("cameras");
		for (int i = 0; i < size; ++i)
		{
			m_cameras.push(LUX_NEW(Camera));
			serializer.deserializeArrayItem(m_cameras[i]->m_far);
			serializer.deserializeArrayItem(m_cameras[i]->m_near);
			serializer.deserializeArrayItem(m_cameras[i]->m_fov);
			serializer.deserializeArrayItem(m_cameras[i]->m_is_active);
			serializer.deserializeArrayItem(m_cameras[i]->m_width);
			serializer.deserializeArrayItem(m_cameras[i]->m_height);
			m_cameras[i]->m_aspect = m_cameras[i]->m_width / m_cameras[i]->m_height;
			serializer.deserializeArrayItem(m_cameras[i]->m_entity.index);
			m_cameras[i]->m_entity.universe = m_universe;
		}
		serializer.deserializeArrayEnd();
		serializer.deserialize("renderable_count", size);
		serializer.deserializeArrayBegin("renderables");
		for (int i = 0; i < size; ++i)
		{
			m_renderables.pushEmpty();
			serializer.deserializeArrayItem(m_renderables[i].m_entity.index);
			m_renderables[i].m_entity.universe = m_universe;
			char path[MAX_PATH];
			serializer.deserializeArrayItem(path, MAX_PATH);
			serializer.deserializeArrayItem(m_renderables[i].m_scale);
			m_renderables[i].m_model = LUX_NEW(ModelInstance)(static_cast<Model&>(*m_engine->getResourceManager().get(ResourceManager::MODEL)->load(path)));
			for (int j = 0; j < 16; ++j)
			{
				serializer.deserializeArrayItem((&m_renderables[i].m_model->getMatrix().m11)[j]);
			}
		}
		serializer.deserializeArrayEnd();
	}

	Engine* m_engine;
	Array<Camera*> m_cameras;
	Array<Light> m_lights;
	Array<Pipeline*> m_pipelines;
	Array<Renderable> m_renderables;
	Array<Model*> m_models;
	Universe* m_universe;
};


Renderer* Renderer::createInstance()
{
	return LUX_NEW(RendererImpl);
}


void Renderer::destroyInstance(Renderer& renderer)
{
	LUX_DELETE(&renderer);
}


} // ~namespace Lux