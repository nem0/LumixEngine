#include "core/mt/spin_mutex.h"
#include "core/mt/atomic.h"
#include "core/mt/thread.h"


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
				if(compareAndExchange(&m_id, 1, 0) == 0)
				{
					memoryBarrier();
					return;
				}

				while(m_id)
				{
					yield();
				}
			}
		}

		bool SpinMutex::poll()
		{
			if (compareAndExchange(&m_id, 1, 0) == 0)
			{
				memoryBarrier();
				return true;
			}
			return false;
		}

		void SpinMutex::unlock()
		{
			memoryBarrier();
			m_id = 0;
		}
	} // ~namespace MT
} // ~namespace Lumix
