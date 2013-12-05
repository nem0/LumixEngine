#include "platform/semaphore.h"
#include <Windows.h>
#include <cassert>

namespace Lux
{
	namespace MT
	{
		class WinSemaphore : public Semaphore
		{
		public:
			virtual void signal() LUX_OVERRIDE;
			
			virtual void wait() LUX_OVERRIDE;
			virtual bool poll() LUX_OVERRIDE;

			WinSemaphore(const char* name, int init_count, int max_count);
			~WinSemaphore();

		private:
			HANDLE m_id;
		};

		Semaphore* Semaphore::create(const char* name, int init_count, int max_count)
		{
			return new WinSemaphore(name, init_count, max_count);
		}

		void Semaphore::destroy(Semaphore* semaphore)
		{
			delete static_cast<WinSemaphore*>(semaphore);
		}

		void WinSemaphore::signal()
		{
			::ReleaseSemaphore(m_id, 1, NULL);
		}

		void WinSemaphore::wait()
		{
			::WaitForSingleObject(m_id, INFINITE);
		}

		bool WinSemaphore::poll()
		{
			return WAIT_OBJECT_0 == ::WaitForSingleObject(m_id, 0);
		}

		WinSemaphore::WinSemaphore(const char* name, int init_count, int max_count)
		{
			m_id = ::CreateSemaphore(NULL, init_count, max_count, name);
		}

		WinSemaphore::~WinSemaphore()
		{
			::CloseHandle(m_id);
		}
	}; // ~namespac MT
} // ~namespace Lux