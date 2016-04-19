#include "platform/spin_mutex.h"
#include <pthread.h>

namespace Lux
{
	namespace MT
	{
		class OSXMutex : public SpinMutex
		{
		public:
			virtual void lock() LUX_OVERRIDE;
			virtual bool poll() LUX_OVERRIDE;
			
			virtual void unlock() LUX_OVERRIDE;
			
			OSXMutex(const char* name, bool locked);
			~OSXMutex();
			
		private:
			pthread_mutex_t m_id;
		};
		
		SpinMutex* SpinMutex::create(const char* name, bool locked /* = false */)
		{
			return new OSXMutex(name, locked);
		}
		
		void SpinMutex::destroy(SpinMutex* spin_mutex)
		{
			delete static_cast<OSXMutex*>(spin_mutex);
		}
		
		void OSXMutex::lock()
		{
			pthread_mutex_lock(&m_id);
		}
		
		bool OSXMutex::poll()
		{
			unsigned int res = pthread_mutex_trylock(&m_id);
			return 0 == res;
		}
		
		void OSXMutex::unlock()
		{
			pthread_mutex_unlock(&m_id);
		}
		
		OSXMutex::OSXMutex(const char* name, bool locked)
		{
			m_id = PTHREAD_MUTEX_INITIALIZER;
			pthread_mutex_init(&m_id, LUX_NULL);
		}
		
		OSXMutex::~OSXMutex()
		{
			pthread_mutex_destroy(&m_id);
		}
	} // ~namespace MT
} // ~namespace Lux