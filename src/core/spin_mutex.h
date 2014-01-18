#pragma once
#include "core/lux.h"

namespace Lux
{
	namespace MT
	{
		struct RTL_CRITICAL_SECTION;
		typedef RTL_CRITICAL_SECTION* SpinMutexHandle;

		class LUX_CORE_API SpinMutex
		{
		public:
			SpinMutex(bool locked);
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
			SpinMutex& m_mutex;
		};
	}; // ~namespace MT
}; // ~namespace Lux