#include "core/mt/spin_mutex.h"
#include <Windows.h>

namespace Lumix
{
	namespace MT
	{
		SpinMutex::SpinMutex(bool locked)
			: m_id(0)
		{
			if(locked)
			{
				lock();
			}
		}

		SpinMutex::~SpinMutex()
		{ }

		void SpinMutex::lock()
		{
			for (;;)
			{
				if(InterlockedCompareExchange((LONG*)&m_id, 1, 0) == 0)
				{
					::MemoryBarrier();
					return;
				}

				while(m_id)
				{
					Sleep(0);
				}
			}
		}

		bool SpinMutex::poll()
		{
			if(InterlockedCompareExchange((LONG*)&m_id, 1, 0) == 0)
			{
				::MemoryBarrier();
				return true;
			}
			return false;
		}

		void SpinMutex::unlock()
		{
			::MemoryBarrier();
			m_id = 0;
		}
	} // ~namespace MT
} // ~namespace Lumix
