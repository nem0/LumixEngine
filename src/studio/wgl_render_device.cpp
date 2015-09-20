#include "wgl_render_device.h"
#include "core/crc32.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "renderer/pipeline.h"
#include <qwidget.h>


WGLRenderDevice::WGLRenderDevice(Lumix::WorldEditor& editor,
								 Lumix::Engine& engine,
								 const char* pipeline_path)
	: m_engine(engine)
	, m_editor(editor)
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
			(Lumix::RenderScene*)editor.getScene(Lumix::crc32("renderer")));
	}
	editor.universeCreated()
		.bind<WGLRenderDevice, &WGLRenderDevice::onUniverseCreated>(this);
	editor.universeDestroyed()
		.bind<WGLRenderDevice, &WGLRenderDevice::onUniverseDestroyed>(this);
}


WGLRenderDevice::~WGLRenderDevice()
{
	ASSERT(!m_pipeline);
}


void WGLRenderDevice::setWidget(QWidget& widget)
{
	auto handle = (HWND)widget.winId();
	getPipeline().setViewport(0, 0, widget.width(), widget.height());
	getPipeline().setWindowHandle(handle);
}


void WGLRenderDevice::onUniverseCreated()
{
	getPipeline().setScene(
		(Lumix::RenderScene*)m_editor.getScene(Lumix::crc32("renderer")));
}


void WGLRenderDevice::onUniverseDestroyed()
{
	getPipeline().setScene(nullptr);
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
	m_editor.universeCreated()
		.unbind<WGLRenderDevice, &WGLRenderDevice::onUniverseCreated>(this);
	m_editor.universeDestroyed()
		.unbind<WGLRenderDevice, &WGLRenderDevice::onUniverseDestroyed>(this);
	if (m_pipeline)
	{
		Lumix::PipelineInstance::destroy(m_pipeline);
		m_pipeline = nullptr;
	}
}