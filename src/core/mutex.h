#pragma once
#include "core/lux.h"
#include <Windows.h>


namespace Lux
{


	class LUX_CORE_API Mutex
	{
		public:
			void create();
			void destroy();
			void lock();
			void unlock();

		private:
			HANDLE m_handle;
			int m_locked;
	};

	class Lock
	{
		public:
			Lock(Mutex& mutex) : m_mutex(mutex) { mutex.lock(); }
			~Lock() { m_mutex.unlock(); }

		private:
			Mutex& m_mutex;
	};
};