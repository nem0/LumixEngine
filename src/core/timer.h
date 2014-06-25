#pragma once


#include "core/lumix.h"


namespace Lumix
{


	class LUMIX_CORE_API Timer
	{
		public:
			/// returns time (seconds) since the last tick() call or since the creation of the timer
			virtual float tick() = 0;
			virtual float getTimeSinceStart() = 0;

			static Timer* create();
			static void destroy(Timer* timer);
	};


} // ~namespace Lumix
