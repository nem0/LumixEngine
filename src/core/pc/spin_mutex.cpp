#include "core/spin_mutex.h"
#include <Windows.h>

namespace Lux
{
	namespace MT
	{
		class WinSpinMutex : public SpinMutex
		{
		public:
			virtual void lock() LUX_OVERRIDE;
			virtual bool poll() LUX_OVERRIDE;

			virtual void unlock() LUX_OVERRIDE;

			WinSpinMutex(bool locked);

		private:
			~WinSpinMutex();

			CRITICAL_SECTION m_id;
		};

		SpinMutex* SpinMutex::create(bool locked)
		{
			return LUX_NEW(WinSpinMutex)(locked);
		}

		void SpinMutex::destroy(SpinMutex* spin_mutex)
		{
			LUX_DELETE(spin_mutex);
		}

		void WinSpinMutex::lock()
		{
			::EnterCriticalSection(&m_id);
		}

		bool WinSpinMutex::poll()
		{
			return TRUE == ::TryEnterCriticalSection(&m_id);
		}

		void WinSpinMutex::unlock()
		{
			::LeaveCriticalSection(&m_id);
		}

		WinSpinMutex::WinSpinMutex(bool locked)
		{
			::InitializeCriticalSectionAndSpinCount(&m_id, 0x00000400);
			if(locked)
			{
				lock();
			}
		}

		WinSpinMutex::~WinSpinMutex()
		{
			::DeleteCriticalSection(&m_id);
		}
	} // ~namespace MT
} // ~namespace Lux