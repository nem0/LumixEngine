#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace MT
	{
		class LUX_PLATFORM_API Task
		{
		public:
			Task();
			~Task();

			virtual int task() = 0;

			bool create(const char* name);
			bool run();
			bool destroy();

			bool setAffinityMask(unsigned int affinity_mask);
			bool setThreadPriority(unsigned int priority);

		private:
			struct TaskImpl* m_implementation;
		};	
	} // ~namespace MT
} // ~namespace Lux
