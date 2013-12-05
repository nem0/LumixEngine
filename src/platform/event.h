#pragma once
#include "core/lux.h"

namespace  Lux
{
	namespace MT
	{
		class LUX_PLATFORM_API Event LUX_ABSTRACT
		{
		public:
			static Event* create(const char* name, bool signaled = false, bool manual_reset = true);
			static void destroy(Event* event);

			virtual void reset() = 0;

			virtual void trigger() = 0;

			virtual void wait() = 0;
			virtual bool poll() = 0;
		};
	}; // ~namespace MT
}; // ~namespace Lux