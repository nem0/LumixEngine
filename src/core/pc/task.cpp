#include "core/lux.h"
#include "core/task.h"
#include <Windows.h>

namespace Lux
{
	namespace MT
	{
		const uint32_t STACK_SIZE = 0x8000;

		void SetThreadName(DWORD thread_id, const char* thread_name);

		
		static uint32_t s_main_thread_id = 0;

		void sleep(uint32_t miliseconds) { ::Sleep(miliseconds); }

		uint32_t getCurrentThreadID() { return ::GetCurrentThreadId(); }

		uint32_t getProccessAffinityMask()
		{
			PROCESSOR_NUMBER proc_number;
			BOOL ret = ::GetThreadIdealProcessorEx(::GetCurrentThread(), &proc_number);
			ASSERT(ret);
			return proc_number.Number;
		}

		bool isMainThread() { return s_main_thread_id == ::GetCurrentThreadId(); }
		void setMainThread() { s_main_thread_id = ::GetCurrentThreadId(); }

		struct TaskImpl
		{
			HANDLE m_handle;
			DWORD m_thread_id;
			uint32_t m_affinity_mask;
			uint32_t m_priority;
			volatile bool m_is_running;
			volatile bool m_force_exit;
			volatile bool m_exited;
			const char* m_thread_name;
			Task* m_owner;
		};

		static DWORD WINAPI threadFunction(LPVOID ptr)
		{
			uint32_t ret = 0xffffFFFF;
			struct TaskImpl* impl = reinterpret_cast<TaskImpl*>(ptr);
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
			TaskImpl* impl = LUX_NEW(TaskImpl);
			impl->m_handle = NULL;
			impl->m_affinity_mask = getProccessAffinityMask();
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
			LUX_DELETE(m_implementation);
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
			return ::ResumeThread(m_implementation->m_handle) != -1;
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

		void Task::setAffinityMask(uint32_t affinity_mask)
		{
			m_implementation->m_affinity_mask = affinity_mask;
			if(m_implementation->m_handle)
			{
				::SetThreadIdealProcessor(m_implementation->m_handle, affinity_mask);
			}
		}

		void Task::setPriority(uint32_t priority)
		{
			m_implementation->m_priority = priority;
			if(m_implementation->m_handle)
			{
				::SetThreadPriority(m_implementation->m_handle, priority);
			}
		}

		uint32_t Task::getAffinityMask() const
		{
			return m_implementation->m_affinity_mask;
		}

		uint32_t Task::getPriority() const
		{
			return m_implementation->m_priority;
		}

		uint32_t Task::getExitCode() const
		{
			uint32_t exit_code = 0xffffFFFF;
			::GetExitCodeThread(m_implementation->m_handle, (LPDWORD)&exit_code);
			return exit_code;
		}

		bool Task::isRunning() const
		{
			return m_implementation->m_is_running;
		}

		bool Task::isFinished() const 
		{ 
			return m_implementation->m_exited; 
		}

		bool Task::isForceExit() const
		{
			return m_implementation->m_force_exit;
		}

		void Task::forceExit(bool wait)
		{
			m_implementation->m_force_exit = true;

			while(!isFinished() && wait)
			{
				Sleep(0);
			}
		}

		void Task::exit(int32_t exitCode)
		{
			m_implementation->m_exited = true;
			m_implementation->m_is_running = false;
			::ExitThread(exitCode);
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