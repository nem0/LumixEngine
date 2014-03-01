#include "editor_native/sdl_render_device.h"


void SDLRenderDevice::endFrame()
{
	SDL_GL_SwapWindow(m_window);
}
