#include "platform/event.h"
#include <windows.h>
#include <cassert>

namespace  Lux
{
	namespace MT
	{
		class WinEvent : public Event 
		{
		public:
			virtual void reset() LUX_OVERRIDE;

			virtual void trigger() LUX_OVERRIDE;

			virtual void wait() LUX_OVERRIDE;
			virtual bool poll() LUX_OVERRIDE;

			WinEvent(const char* name, bool signaled, bool manual_reset);
			~WinEvent();

		private:
			HANDLE m_id;
		};

		Event* Event::create(const char* name, bool signaled /* = false */, bool manual_reset /* = true */)
		{
			return new WinEvent(name, signaled, manual_reset);
		}

		void Event::destroy(Event* event)
		{
			delete static_cast<WinEvent*>(event);
		}

		void WinEvent::reset()
		{
			::ResetEvent(m_id);
		}

		void WinEvent::trigger()
		{
			::SetEvent(m_id);
		}

		void WinEvent::wait()
		{
			::WaitForSingleObject(m_id, INFINITE);
		}

		bool WinEvent::poll()
		{
			return WAIT_OBJECT_0 == ::WaitForSingleObject(m_id, 0);
		}

		WinEvent::WinEvent(const char* name, bool signaled, bool manual_reset)
		{
			m_id = ::CreateEvent(NULL, manual_reset, signaled, name);
		}

		WinEvent::~WinEvent()
		{
			::CloseHandle(m_id);
		}
	}; // ~namespace MT
}; // ~namespace Lux