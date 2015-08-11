#include "lumix.h"
#include "core/mt/thread.h"
#include <Windows.h>

namespace Lumix
{
	namespace MT
	{
		static uint32_t s_main_thread_id = 0;

		void sleep(uint32_t milliseconds) { ::Sleep(milliseconds); }

		uint32_t getCPUsCount()
		{
			SYSTEM_INFO sys_info;
			GetSystemInfo(&sys_info);

			uint32_t num = sys_info.dwNumberOfProcessors;
			num = num > 0 ? num : 1;

			return num;
		}

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

		void setThreadName(uint32_t thread_id, const char* thread_name)
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