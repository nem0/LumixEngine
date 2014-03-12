#pragma once

#include <SDL.h>
#include "core/lux.h"
#include "graphics/irender_device.h"

namespace Lux
{
	class Renderer;
};


class SDLRenderDevice : public Lux::IRenderDevice
{
	public:
		SDLRenderDevice(SDL_Window* window, Lux::Renderer* renderer) : m_window(window), m_renderer(renderer) {}

		virtual void endFrame() LUX_OVERRIDE;
		virtual Lux::Pipeline& getPipeline() LUX_OVERRIDE;

	private:
		SDL_Window* m_window;
		Lux::Renderer* m_renderer;
};
