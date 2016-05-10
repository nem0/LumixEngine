#include "platform/semaphore.h"
#include <sys/semaphore.h>
#include <cassert>

namespace Lux
{
	namespace MT
	{
		class OSXSemaphore : public Semaphore
		{
		public:
			virtual void signal() LUX_OVERRIDE;
			
			virtual void wait() LUX_OVERRIDE;
			virtual bool poll() LUX_OVERRIDE;

			OSXSemaphore(const char* name, int init_count, int max_count);
			~OSXSemaphore();

		private:
			sem_t m_id;
		};

		Semaphore* Semaphore::create(const char* name, int init_count, int max_count)
		{
			return new OSXSemaphore(name, init_count, max_count);
		}

		void Semaphore::destroy(Semaphore* sempahore)
		{
			delete static_cast<OSXSemaphore*>(sempahore);
		}

		void OSXSemaphore::signal()
		{
			::sem_post(&m_id);
		}

		void OSXSemaphore::wait()
		{
			::sem_wait(&m_id);
		}

		bool OSXSemaphore::poll()
		{
			return 0 == ::sem_trywait(&m_id);
		}

		OSXSemaphore::OSXSemaphore(const char* name, int init_count, int max_count)
		{
			::sem_init(&m_id, 0, init_count);
		}

		OSXSemaphore::~OSXSemaphore()
		{
			::sem_destroy(&m_id);
		}
	}; // ~namespac MT
} // ~namespace Lux