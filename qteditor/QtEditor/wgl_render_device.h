#pragma once

#include <Windows.h>
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "graphics/irender_device.h"


class WGLRenderDevice : public Lux::IRenderDevice
{
public:
	WGLRenderDevice(Lux::Engine& engine, const char* pipeline_path)
	{
		Lux::Pipeline* pipeline_object = static_cast<Lux::Pipeline*>(engine.getResourceManager().get(Lux::ResourceManager::PIPELINE)->load(pipeline_path));
		ASSERT(pipeline_object);
		if(pipeline_object)
		{
			m_pipeline = Lux::PipelineInstance::create(*pipeline_object);
			m_pipeline->setRenderer(engine.getRenderer());
		}

	}

	~WGLRenderDevice()
	{
		if(m_pipeline)
		{
			Lux::PipelineInstance::destroy(m_pipeline);
		}
	}

	virtual void beginFrame() override
	{
		PROFILE_FUNCTION();
		BOOL b = wglMakeCurrent(m_hdc, m_opengl_context);
		ASSERT(b);
	}

	virtual void endFrame() override
	{
		PROFILE_FUNCTION();
		BOOL b = wglSwapLayerBuffers(m_hdc, WGL_SWAP_MAIN_PLANE);
		ASSERT(b);
	}

	virtual Lux::PipelineInstance& getPipeline()
	{
		return *m_pipeline;
	}

	Lux::PipelineInstance* m_pipeline;
	HDC m_hdc;
	HGLRC m_opengl_context;
};
