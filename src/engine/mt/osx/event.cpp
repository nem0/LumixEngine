#include "platform/event.h"
#include <cassert>
#include <pthread.h>

namespace  Lux
{
	namespace MT
	{
		class OSXEvent : public Event 
		{
		public:
			virtual void reset() LUX_OVERRIDE;

			virtual void trigger() LUX_OVERRIDE;

			virtual void wait() LUX_OVERRIDE;
			virtual bool poll() LUX_OVERRIDE;

			OSXEvent(const char* name, bool signaled, bool manual_reset);
			~OSXEvent();

		private:
			pthread_mutex_t m_mutex;
			pthread_cond_t m_cond;
			unsigned int m_waiting_thrads;
			bool m_manual_reset;
			bool m_signaled;
		};

		Event* Event::create(const char* name, bool signaled /* = false */, bool manual_reset /* = true */)
		{
			return new OSXEvent(name, signaled, manual_reset);
		}

		void Event::destroy(Event* event)
		{
			delete static_cast<OSXEvent*>(event);
		}

		void OSXEvent::reset()
		{
			pthread_mutex_lock(&m_mutex);
			m_signaled = false;
			pthread_mutex_unlock(&m_mutex);
		}

		void OSXEvent::trigger()
		{
			pthread_mutex_lock(&m_mutex);
			if(m_manual_reset)
			{
				m_signaled = true;
				pthread_cond_broadcast(&m_cond);
			}
			else
			{
				if (m_waiting_thrads == 0)
				{
					m_signaled = true;
				}
				else
				{
					pthread_cond_signal(&m_cond);
				}
			}
			pthread_mutex_unlock(&m_mutex);
		}

		void OSXEvent::wait()
		{
			pthread_mutex_lock(&m_mutex);
			if (m_signaled)
			{
				if (false == m_manual_reset)
				{
					m_signaled = false;
				}
			}
			else
			{
				m_waiting_thrads++;
				pthread_cond_wait(&m_cond, &m_mutex);
				m_waiting_thrads--;
			}
			pthread_mutex_unlock(&m_mutex);
		}

		bool OSXEvent::poll()
		{
			assert(0 && "Not supported!");
			return false;
		}

		OSXEvent::OSXEvent(const char* name, bool signaled, bool manual_reset)
		{
			m_mutex = PTHREAD_MUTEX_INITIALIZER;
			m_cond = PTHREAD_COND_INITIALIZER;
			
			pthread_mutex_init(&m_mutex, LUX_NULL);
			pthread_cond_init(&m_cond, LUX_NULL);
			m_waiting_thrads = 0;
			m_manual_reset = manual_reset;
			m_signaled = signaled;
		}

		OSXEvent::~OSXEvent()
		{
			pthread_mutex_destroy(&m_mutex);
			pthread_cond_destroy(&m_cond);
		}
	}; // ~namespace MT
}; // ~namespace Lux