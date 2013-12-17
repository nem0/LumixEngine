#pragma once
#include "core/lux.h"

namespace Lux
{
	namespace MT
	{
		class LUX_PLATFORM_API Mutex LUX_ABSTRACT
		{
		public:
			static Mutex* create(bool locked = false);
			static void destroy(Mutex* mutex);

			virtual void lock() = 0;
			virtual bool poll() = 0;

			virtual void unlock() = 0;
		};

		class Lock
		{
		public:
			Lock(Mutex& mutex) : m_mutex(mutex) { mutex.lock(); }
			~Lock() { m_mutex.unlock(); }

		private:
			Mutex& m_mutex;
		};
	} // ~namespace MT
}; // ~namespace Lux