#include "editor_native/sdl_render_device.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"

void SDLRenderDevice::endFrame()
{
	SDL_GL_SwapWindow(m_window);
}


Lux::Pipeline& SDLRenderDevice::getPipeline()
{
	/// TODO pipeline manager
	static Lux::Pipeline* p = m_renderer->loadPipeline("pipelines/main.json");
	return *p;
}