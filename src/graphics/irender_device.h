#pragma once

#include "core/lux.h"


namespace Lux
{


class Pipeline;


class LUX_ENGINE_API IRenderDevice LUX_ABSTRACT
{
	public:
		virtual ~IRenderDevice() {}

		virtual void endFrame() = 0;
		virtual Pipeline& getPipeline() = 0;
};


} // ~namespace Lux