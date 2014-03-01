#pragma once

#include "core/lux.h"


namespace Lux
{


class LUX_ENGINE_API IRenderDevice LUX_ABSTRACT
{
	public:
		virtual ~IRenderDevice() {}

		virtual void endFrame() = 0;
};


} // ~namespace Lux