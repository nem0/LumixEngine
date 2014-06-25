#pragma once

#include "core/lumix.h"


namespace Lumix
{


class PipelineInstance;


class LUMIX_ENGINE_API IRenderDevice abstract
{
	public:
		virtual ~IRenderDevice() {}

		virtual void beginFrame() = 0;
		virtual void endFrame() = 0;
		virtual PipelineInstance& getPipeline() = 0;
};


} // ~namespace Lumix
