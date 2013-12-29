#include "platform/mutex.h"
#include <Windows.h>
#include <cassert>


namespace Lux
{
	namespace MT
	{
		class WinMutex : public Mutex
		{
		public:
			virtual void lock() LUX_OVERRIDE;
			virtual bool poll() LUX_OVERRIDE;

			virtual void unlock() LUX_OVERRIDE;

			WinMutex(bool locked);
			~WinMutex();

		private:
			HANDLE m_id;
			int m_locked;
		};

		Mutex* Mutex::create(bool locked)
		{
			return new WinMutex(locked);
		}

		void Mutex::destroy(Mutex* mutex)
		{
			delete static_cast<WinMutex*>(mutex);
		}

		void WinMutex::lock()
		{
			::WaitForSingleObject(m_id, INFINITE);
			ASSERT(m_locked == 0 && "Recursive lock is forbiden!");
			++m_locked;
		}

		bool WinMutex::poll()
		{
			uint32_t res = ::WaitForSingleObject(m_id, 0);
			ASSERT(m_locked == 0 && "Recursive lock is forbiden!");
			m_locked += res ? 1 : 0;
			return res > 0;
		}

		void WinMutex::unlock()
		{
			ASSERT(m_locked);
			--m_locked;
			::ReleaseMutex(m_id);
		}

		WinMutex::WinMutex(bool locked)
		{
			m_id = ::CreateMutex(NULL, locked, NULL);
			m_locked = 0;
		}

		WinMutex::~WinMutex()
		{
			::CloseHandle(m_id);
		}
	} // ~namespace MT
} // ~namespace Lux