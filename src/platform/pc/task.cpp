#include "core/lux.h"
#include "platform/task.h"
#include <Windows.h>

namespace Lux
{
	namespace MT
	{
		void SetThreadName(DWORD thread_id, const char* thread_name);

		const unsigned int STACK_SIZE = 0x8000;

		struct TaskImpl
		{
			HANDLE m_handle;
			DWORD m_thread_id;
			unsigned int m_affinity_mask;
			unsigned int m_priority;
			volatile bool m_is_running;
			volatile bool m_force_exit; //TODO: m_force_exit
			volatile bool m_exited;
			const char* m_thread_name;
			Task* m_owner;
		};

		static DWORD WINAPI threadFunction(LPVOID ptr)
		{
			unsigned int ret = -1;
			struct TaskImpl* impl =  reinterpret_cast<TaskImpl*>(ptr);
			if(!impl->m_force_exit)
			{
				impl->m_is_running = true;
				ret = impl->m_owner->task();
			}
			impl->m_exited = true;
			impl->m_is_running = false;

			return ret;
		}

		Task::Task()
		{
			TaskImpl* impl = new TaskImpl();
			impl->m_handle = NULL;
			impl->m_affinity_mask = 0; //TODO ::GetProcessAffinityMask(Getcur);
			impl->m_priority = ::GetThreadPriority(GetCurrentThread());
			impl->m_is_running = false;
			impl->m_force_exit = false;
			impl->m_exited = false;
			impl->m_thread_name = "";
			impl->m_owner = this;

			m_implementation = impl;
		}

		Task::~Task()
		{
			ASSERT(NULL == m_implementation->m_handle);
			delete m_implementation;
		}

		bool Task::create(const char* name)
		{
			HANDLE handle = CreateThread(NULL, STACK_SIZE, threadFunction, m_implementation, CREATE_SUSPENDED, &m_implementation->m_thread_id);
			if (handle)
			{
				SetThreadName(m_implementation->m_thread_id, name);
				m_implementation->m_thread_name = name;
				m_implementation->m_handle = handle;
			}
			return handle != NULL;
		}

		bool Task::run()
		{
			return ResumeThread(m_implementation->m_handle) != -1;
		}

		bool Task::destroy()
		{
			while(m_implementation->m_is_running)
			{
				Sleep(0);
			}

			::CloseHandle(m_implementation->m_handle);
			m_implementation->m_handle = NULL;
			return true;
		}

		bool Task::isFinished() const 
		{ 
			return m_implementation->m_exited; 
		}

		static const DWORD MS_VC_EXCEPTION=0x406D1388;

		#pragma pack(push,8)
		typedef struct tagTHREADNAME_INFO
		{

			DWORD type;
			LPCSTR name;
			DWORD thread_id;
			DWORD flags; 
		} THREADNAME_INFO;
		#pragma pack(pop)

		void SetThreadName(DWORD thread_id, const char* thread_name)
		{
			THREADNAME_INFO info;
			info.type = 0x1000;
			info.name = thread_name;
			info.thread_id = thread_id;
			info.flags = 0;

			__try
			{
				RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info );
			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
			}
		}
	} // ~namespace MT
} // ~namespace Lux