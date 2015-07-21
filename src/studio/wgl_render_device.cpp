#include "wgl_render_device.h"
#include "core/crc32.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/pipeline.h"
#include <qwidget.h>


WGLRenderDevice::WGLRenderDevice(Lumix::Engine& engine,
								 const char* pipeline_path)
	: m_engine(engine)
{
	Lumix::Pipeline* pipeline_object =
		static_cast<Lumix::Pipeline*>(engine.getResourceManager()
										  .get(Lumix::ResourceManager::PIPELINE)
										  ->load(Lumix::Path(pipeline_path)));
	ASSERT(pipeline_object);
	if (pipeline_object)
	{
		m_pipeline = Lumix::PipelineInstance::create(*pipeline_object,
													 engine.getAllocator());
		m_pipeline->setScene(
			(Lumix::RenderScene*)engine.getScene(crc32("renderer")));
	}
	engine.getWorldEditor()
		->universeCreated()
		.bind<WGLRenderDevice, &WGLRenderDevice::onUniverseCreated>(this);
	engine.getWorldEditor()
		->universeDestroyed()
		.bind<WGLRenderDevice, &WGLRenderDevice::onUniverseDestroyed>(this);
}


WGLRenderDevice::~WGLRenderDevice()
{
	ASSERT(!m_pipeline);
}


void WGLRenderDevice::setWidget(QWidget& widget)
{
	auto handle = (HWND)widget.winId();
	getPipeline().resize(widget.width(), widget.height());
	getPipeline().setWindowHandle(handle);
}


void WGLRenderDevice::onUniverseCreated()
{
	getPipeline().setScene(
		(Lumix::RenderScene*)m_engine.getScene(crc32("renderer")));
}


void WGLRenderDevice::onUniverseDestroyed()
{
	getPipeline().setScene(NULL);
}


Lumix::PipelineInstance& WGLRenderDevice::getPipeline()
{
	return *m_pipeline;
}


int WGLRenderDevice::getWidth() const
{
	return m_pipeline->getWidth();
}


int WGLRenderDevice::getHeight() const
{
	return m_pipeline->getHeight();
}


void WGLRenderDevice::shutdown()
{
	m_engine.getWorldEditor()
		->universeCreated()
		.unbind<WGLRenderDevice, &WGLRenderDevice::onUniverseCreated>(this);
	m_engine.getWorldEditor()
		->universeDestroyed()
		.unbind<WGLRenderDevice, &WGLRenderDevice::onUniverseDestroyed>(this);
	if (m_pipeline)
	{
		Lumix::PipelineInstance::destroy(m_pipeline);
		m_pipeline = nullptr;
	}
}