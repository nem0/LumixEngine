#include "engine/core/lux.h"
#include "platform/task.h"
#include <pthread.h>

namespace Lux
{
	namespace MT
	{
		const unsigned int STACK_SIZE = 0x8000;
		
		struct TaskImpl
		{
			pthread_t m_id;
			unsigned int m_affinity_mask;
			unsigned int m_priority;
			volatile bool m_is_running;
			volatile bool m_force_exit;
			volatile bool m_exited;
			const char* m_thread_name;
			Task* m_owner;
		};
		
		static void* threadFunction(void* ptr)
		{
			unsigned int ret = -1;
			struct TaskImpl* impl =  reinterpret_cast<TaskImpl*>(ptr);
			pthread_setname_np(impl->m_thread_name);

			if(!impl->m_force_exit)
			{
				impl->m_is_running = true;
				ret = impl->m_owner->task();
			}
			impl->m_exited = true;
			impl->m_is_running = false;
			
			pthread_exit(&ret);
		}
		
		Task::Task()
		{
			TaskImpl* impl = new TaskImpl();
			impl->m_affinity_mask = 0; //TODO ::GetProcessAffinityMask(Getcur);
			impl->m_priority = 0; //TODO: ::GetThreadPriority(GetCurrentThread());
			impl->m_is_running = false;
			impl->m_force_exit = false;
			impl->m_exited = false;
			impl->m_thread_name = "";
			impl->m_owner = this;
			
			m_implementation = impl;
		}
		
		Task::~Task()
		{
			delete m_implementation;
		}
		
		bool Task::create(const char* name)
		{
			m_implementation->m_thread_name = name;
			return true;
		}
		
		bool Task::run()
		{
			bool ret = false;
			if(0 == pthread_create(&m_implementation->m_id, LUX_NULL, &threadFunction, m_implementation))
			{
				ret = true;
			}
			return ret;
		}
		
		bool Task::destroy()
		{
			pthread_join(m_implementation->m_id, LUX_NULL);
			return true;
		}
	} // ~namespace MT
} // ~namespace Lux
