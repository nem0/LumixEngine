#pragma once


#include "core/lux.h"


namespace Lux
{


	class LUX_CORE_API Timer
	{
		public:
			/// returns time (seconds) since the last tick() call or since the creation of the timer
			virtual float tick() = 0;

			static Timer* create();
			static void destroy(Timer* timer);
	};


} // ~namespace Lux