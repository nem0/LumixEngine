#include "core/spin_mutex.h"
#include <Windows.h>

namespace Lux
{
	namespace MT
	{
		SpinMutex::SpinMutex(bool locked)
		{
			::InitializeCriticalSectionAndSpinCount((LPCRITICAL_SECTION)m_id, 0x00000400);
			if(locked)
			{
				lock();
			}
		}

		SpinMutex::~SpinMutex()
		{
			::DeleteCriticalSection((LPCRITICAL_SECTION)m_id);
		}

		void SpinMutex::lock()
		{
			::EnterCriticalSection((LPCRITICAL_SECTION)m_id);
		}

		bool SpinMutex::poll()
		{
			return TRUE == ::TryEnterCriticalSection((LPCRITICAL_SECTION)m_id);
		}

		void SpinMutex::unlock()
		{
			::LeaveCriticalSection((LPCRITICAL_SECTION)m_id);
		}
	} // ~namespace MT
} // ~namespace Lux