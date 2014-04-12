#pragma once
#include "core/lux.h"

namespace Lux
{
	namespace MT
	{
		typedef volatile int32_t SpinMutexHandle;

		class LUX_CORE_API SpinMutex
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
}; // ~namespace Lux