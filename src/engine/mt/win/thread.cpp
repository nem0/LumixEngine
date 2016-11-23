#include "engine/lumix.h"
#include "engine/mt/thread.h"
#include "engine/win/simple_win.h"


namespace Lumix
{
	namespace MT
	{
		void sleep(u32 milliseconds) { ::Sleep(milliseconds); }
		void yield() { sleep(0); }
		
		u32 getCPUsCount()
		{
			SYSTEM_INFO sys_info;
			GetSystemInfo(&sys_info);

			u32 num = sys_info.dwNumberOfProcessors;
			num = num > 0 ? num : 1;

			return num;
		}

		ThreadID getCurrentThreadID() { return ::GetCurrentThreadId(); }

		u32 getThreadAffinityMask()
		{
			PROCESSOR_NUMBER proc_number;
			BOOL ret = ::GetThreadIdealProcessorEx(::GetCurrentThread(), &proc_number);
			ASSERT(ret);
			return proc_number.Number;
		}

		static const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
		typedef struct tagTHREADNAME_INFO
		{
			DWORD type;
			LPCSTR name;
			DWORD thread_id;
			DWORD flags;
		} THREADNAME_INFO;
#pragma pack(pop)

		void setThreadName(ThreadID thread_id, const char* thread_name)
		{
			THREADNAME_INFO info;
			info.type = 0x1000;
			info.name = thread_name;
			info.thread_id = thread_id;
			info.flags = 0;

			__try
			{
				RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
		}
	} //!namespace MT
} //!namespace Lumix
