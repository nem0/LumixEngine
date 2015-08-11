#pragma once
#include "lumix.h"

namespace Lumix
{
	namespace MT
	{
		typedef void* SemaphoreHandle;

		class LUMIX_ENGINE_API Semaphore
		{
		public:
			Semaphore(int init_count, int max_count);
			~Semaphore();

			void signal();

			void wait();
			bool poll();

		private:
			SemaphoreHandle m_id;
		};
	}; // ~namespac MT
}; //~namespace Lumix
