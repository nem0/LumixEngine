#pragma once

#include <SDL.h>
#include "core/lux.h"
#include "graphics/irender_device.h"


class SDLRenderDevice : public Lux::IRenderDevice
{
	public:
		SDLRenderDevice(SDL_Window* window) : m_window(window) {}

		virtual void endFrame() LUX_OVERRIDE;

	private:
		SDL_Window* m_window;
};
