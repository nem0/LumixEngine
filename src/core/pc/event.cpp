#include "core/event.h"
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

			WinEvent(bool signaled, bool manual_reset);

		private:
			~WinEvent();

			HANDLE m_id;
		};

		Event* Event::create(EventFlags flags)
		{
			return LUX_NEW(WinEvent)(flags & EventFlags::SIGNALED ? true : false, flags & EventFlags::MANUAL_RESET ? true : false);
		}

		void Event::destroy(Event* event)
		{
			LUX_DELETE(event);
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

		WinEvent::WinEvent(bool signaled, bool manual_reset)
		{
			m_id = ::CreateEvent(NULL, manual_reset, signaled, NULL);
		}

		WinEvent::~WinEvent()
		{
			::CloseHandle(m_id);
		}
	}; // ~namespace MT
}; // ~namespace Lux