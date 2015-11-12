#include "core/mt/semaphore.h"
#include "core/pc/simple_win.h"
#include <Windows.h>
#include <cassert>

namespace Lumix
{
	namespace MT
	{
		Semaphore::Semaphore(int init_count, int max_count)
		{
			m_id = ::CreateSemaphore(nullptr, init_count, max_count, nullptr);
		}

		Semaphore::~Semaphore()
		{
			::CloseHandle(m_id);
		}

		void Semaphore::signal()
		{
			::ReleaseSemaphore(m_id, 1, nullptr);
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
} // ~namespace Lumix
