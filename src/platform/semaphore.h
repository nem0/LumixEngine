#pragma once
#include "core/lux.h"

namespace Lux
{
	namespace MT
	{
		class LUX_PLATFORM_API Semaphore LUX_ABSTRACT
		{
		public:
			static Semaphore* create(const char* name, int init_count, int max_count);
			static void destroy(Semaphore*);

			virtual void signal() = 0;

			virtual void wait() = 0;
			virtual bool poll() = 0;
		};
	}; // ~namespac MT
}; //~namespace Lux