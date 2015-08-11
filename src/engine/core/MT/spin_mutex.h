#pragma once
#include "lumix.h"

namespace Lumix
{
	namespace MT
	{
		typedef volatile int32_t SpinMutexHandle;

		class LUMIX_ENGINE_API SpinMutex
		{
		public:
			explicit SpinMutex(bool locked);
			~SpinMutex();

			void lock();
			bool poll();

			void unlock();

		private:
			SpinMutexHandle m_id;
		};

		class SpinLock
		{
		public:
			SpinLock(SpinMutex& mutex) : m_mutex(mutex) { mutex.lock(); }
			~SpinLock() { m_mutex.unlock(); }

		private:
			void operator=(const SpinLock&);
			SpinMutex& m_mutex;
		};
	}; // ~namespace MT
}; // ~namespace Lumix
