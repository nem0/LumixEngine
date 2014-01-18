#include "core/semaphore.h"
#include <Windows.h>
#include <cassert>

namespace Lux
{
	namespace MT
	{
		Semaphore::Semaphore(int init_count, int max_count)
		{
			m_id = ::CreateSemaphore(NULL, init_count, max_count, NULL);
		}

		Semaphore::~Semaphore()
		{
			::CloseHandle(m_id);
		}

		void Semaphore::signal()
		{
			::ReleaseSemaphore(m_id, 1, NULL);
		}

		void Semaphore::wait()
		{
			::WaitForSingleObject(m_id, INFINITE);
		}

		bool Semaphore::poll()
		{
			return WAIT_OBJECT_0 == ::WaitForSingleObject(m_id, 0);
		}
	}; // ~namespac MT
} // ~namespace Lux