#pragma once
#include "core/lux.h"

namespace Lux
{
	namespace MT
	{
		class LUX_PLATFORM_API SpinMutex LUX_ABSTRACT
		{
		public:
			static SpinMutex* create(bool locked = false);
			static void destroy(SpinMutex* spin_mutex);

			virtual void lock() = 0;
			virtual bool poll() = 0;

			virtual void unlock() = 0;
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