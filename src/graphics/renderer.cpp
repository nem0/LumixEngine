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
#include "graphics/pipeline.h"
#include "universe/universe.h"


namespace Lux
{


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
		Pipeline* pipeline = Pipeline::create();
		m_pipelines.push(pipeline);
		pipeline->load("pipelines/main.json", engine.getFileSystem());
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

	Engine* m_engine;
	Array<Camera*> m_cameras;
	PODArray<Camera*> m_active_cameras;
	PODArray<Pipeline*> m_pipelines;
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