#pragma once

#include <Windows.h>
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "graphics/irender_device.h"


class WGLRenderDevice : public Lumix::IRenderDevice
{
public:
	WGLRenderDevice(Lumix::Engine& engine, const char* pipeline_path)
	{
		Lumix::Pipeline* pipeline_object = static_cast<Lumix::Pipeline*>(engine.getResourceManager().get(Lumix::ResourceManager::PIPELINE)->load(Lumix::Path(pipeline_path)));
		ASSERT(pipeline_object);
		if(pipeline_object)
		{
			m_pipeline = Lumix::PipelineInstance::create(*pipeline_object, engine.getAllocator());
			m_pipeline->setRenderer(engine.getRenderer());
		}

	}

	~WGLRenderDevice()
	{
		if(m_pipeline)
		{
			Lumix::PipelineInstance::destroy(m_pipeline);
		}
	}

	virtual void beginFrame() override
	{
		PROFILE_FUNCTION();
		wglMakeCurrent(m_hdc, m_opengl_context);
	}

	virtual void endFrame() override
	{
		PROFILE_FUNCTION();
		wglSwapLayerBuffers(m_hdc, WGL_SWAP_MAIN_PLANE);
	}

	virtual Lumix::PipelineInstance& getPipeline()
	{
		return *m_pipeline;
	}

	Lumix::PipelineInstance* m_pipeline;
	HDC m_hdc;
	HGLRC m_opengl_context;
};
