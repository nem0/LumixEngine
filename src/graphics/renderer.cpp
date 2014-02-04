#include "graphics/renderer.h"
#include <Windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include "core/array.h"
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/json_serializer.h"
#include "core/pod_array.h"
#include "engine/engine.h"
#include "graphics/gl_ext.h"
#include "graphics/irender_device.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/pipeline.h"
#include "graphics/texture.h"
#include "universe/universe.h"


namespace Lux
{


struct Renderable
{
	ModelInstance* m_model;
	Entity m_entity;
};


struct Camera
{
	Pipeline* m_pipeline;
	Entity m_entity;
	bool m_is_active;
};


struct RendererImpl : public Renderer
{

	void applyCamera(Camera* camera)
	{
		Matrix mtx;
		camera->m_entity.getMatrix(mtx);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(90, 1, 0.1f, 100.0f);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		Vec3 pos = mtx.getTranslation();
		Vec3 center = pos + mtx.getZVector();
		Vec3 up = mtx.getYVector();
		gluLookAt(pos.x, pos.y, pos.z, center.x, center.y, center.z, up.x, up.y, up.z);
	}


	virtual void render(IRenderDevice& device) LUX_OVERRIDE
	{
		for(int i = 0; i < m_active_cameras.size(); ++i)
		{
			Camera* camera = m_active_cameras[i];
			applyCamera(camera);
			camera->m_pipeline->render();
		}
		device.endFrame();
	}


	virtual bool create(Engine& engine) LUX_OVERRIDE
	{
		m_engine = &engine;
		loadGLExtensions();
		
		Lux::FS::ReadCallback cb;

		/// TODO remove
		Pipeline* pipeline = Pipeline::create(*this);
		m_pipelines.push(pipeline);
		pipeline->load("pipelines/main.json", engine.getFileSystem());

		glEnable(GL_DEPTH_TEST);

		return true;
	}


	virtual void destroy() LUX_OVERRIDE
	{
	}


	virtual const char* getName() const LUX_OVERRIDE
	{
		return "renderer";
	}


	virtual Component createComponent(uint32_t type, const Entity& entity) LUX_OVERRIDE
	{
		if(type == crc32("camera"))
		{
			Camera* camera = LUX_NEW(Camera);
			camera->m_is_active = false;
			camera->m_entity = entity;
			m_cameras.push(camera);
			return Component(entity, type, this, m_cameras.size() - 1);
		}
		else if(type == crc32("renderable"))
		{
			Renderable& r = m_renderables.pushEmpty();
			r.m_entity = entity;
			r.m_model = NULL;
			return Component(entity, type, this, m_renderables.size() - 1);
		}
		return Component::INVALID;
	}


	virtual void setCameraActive(Component cmp, const bool& active) LUX_OVERRIDE
	{
		if(m_cameras[cmp.index]->m_is_active != active)
		{
			m_cameras[cmp.index]->m_is_active = active;
			if(active)
			{
				m_active_cameras.push(m_cameras[cmp.index]);
			}
			else
			{
				for(int i = 0; i < m_active_cameras.size(); ++i)
				{
					if(m_active_cameras[i] == m_cameras[cmp.index])
					{
						m_active_cameras.eraseFast(i);
						break;
					}
				}
			}
		}
	}


	virtual void setRenderablePath(Component cmp, const string& path) LUX_OVERRIDE
	{
		LUX_DELETE(m_renderables[cmp.index].m_model);
		Renderable& r = m_renderables[cmp.index];
		Model* model = LUX_NEW(Model)(*this);
		model->load(path.c_str(), m_engine->getFileSystem());
		r.m_model = LUX_NEW(ModelInstance)(*model);
	}


	virtual void getRenderableInfos(PODArray<RenderableInfo>& infos) LUX_OVERRIDE
	{
		for(int i = 0; i < m_renderables.size(); ++i)
		{
			RenderableInfo& info = infos.pushEmpty();
			info.m_model_instance = m_renderables[i].m_model;	
		}
	}


	virtual void setCameraPipeline(Component cmp, const string& pipeline) LUX_OVERRIDE
	{
		m_cameras[cmp.index]->m_pipeline = getPipeline(pipeline.c_str());
	}


	virtual void getCameraActive(Component cmp, bool& active) LUX_OVERRIDE
	{
		active = m_cameras[cmp.index]->m_is_active;
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
		return NULL;
	}


	virtual Material* loadMaterial(const char* path) LUX_OVERRIDE
	{
		/// TODO material manager
		Material* material = LUX_NEW(Material)(*this);
		material->load(path, m_engine->getFileSystem());
		return material;
	}


	virtual Texture* loadTexture(const char* path) LUX_OVERRIDE
	{
		/// TODO texture manager
		Texture* texture = LUX_NEW(Texture);
		texture->load(path, m_engine->getFileSystem());
		return texture;
	}


	Engine* m_engine;
	Array<Camera*> m_cameras;
	PODArray<Camera*> m_active_cameras;
	PODArray<Pipeline*> m_pipelines;
	Array<Renderable> m_renderables;
	PODArray<Model*> m_models;
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