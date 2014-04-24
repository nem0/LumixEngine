#include "platform/mutex.h"
#include <cassert>
#include <pthread.h>


namespace Lux
{
	namespace MT
	{
		class OSXMutex : public Mutex
		{
		public:
			virtual void lock() LUX_OVERRIDE;
			virtual bool poll() LUX_OVERRIDE;
			
			virtual void unlock() LUX_OVERRIDE;
			
			OSXMutex(const char* name, bool locked);
			~OSXMutex();
			
		private:
			pthread_mutex_t m_id;
			int m_locked;
		};
		
		Mutex* Mutex::create(const char* name, bool locked /* = false */)
		{
			return new OSXMutex(name, locked);
		}
		
		void Mutex::destroy(Mutex* mutex)
		{
			delete static_cast<OSXMutex*>(mutex);
		}
		
		void OSXMutex::lock()
		{
			pthread_mutex_lock(&m_id);
			assert(m_locked == 0 && "Recursive lock is forbiden!");
			++m_locked;
		}
		
		bool OSXMutex::poll()
		{
			unsigned int res = pthread_mutex_trylock(&m_id);
			assert(m_locked == 0 && "Recursive lock is forbiden!");
			m_locked += 0 == res ? 1 : 0;
			return 0 == res;
		}
		
		void OSXMutex::unlock()
		{
			assert(m_locked);
			--m_locked;
			pthread_mutex_unlock(&m_id);
		}
		
		OSXMutex::OSXMutex(const char* name, bool locked)
		{
			m_id = PTHREAD_MUTEX_INITIALIZER;
			pthread_mutex_init(&m_id, LUX_NULL);
			m_locked = 0;
		}
		
		OSXMutex::~OSXMutex()
		{
			pthread_mutex_destroy(&m_id);
		}
	} // ~namespace MT
} // ~namespace Lux