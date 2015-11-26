#include "lumix.h"
#include "core/iallocator.h"
#include "core/mt/task.h"
#include "core/mt/thread.h"
#include "core/pc/simple_win.h"
#include "core/profiler.h"
#include <Windows.h>


namespace Lumix
{
	namespace MT
	{
		const uint32 STACK_SIZE = 0x8000;

		struct TaskImpl
		{
			TaskImpl(IAllocator& allocator)
				: m_allocator(allocator)
			{ }

			IAllocator& m_allocator;
			HANDLE m_handle;
			DWORD m_thread_id;
			uint32 m_affinity_mask;
			uint32 m_priority;
			volatile bool m_is_running;
			volatile bool m_force_exit;
			volatile bool m_exited;
			const char* m_thread_name;
			Task* m_owner;
		};

		static DWORD WINAPI threadFunction(LPVOID ptr)
		{
			uint32 ret = 0xffffFFFF;
			struct TaskImpl* impl = reinterpret_cast<TaskImpl*>(ptr);
			setThreadName(impl->m_thread_id, impl->m_thread_name);
			Profiler::setThreadName(impl->m_thread_name);
			if (!impl->m_force_exit)
			{
				ret = impl->m_owner->task();
			}
			impl->m_exited = true;
			impl->m_is_running = false;

			return ret;
		}

		Task::Task(IAllocator& allocator)
		{
			TaskImpl* impl = LUMIX_NEW(allocator, TaskImpl)(allocator);
			impl->m_handle = nullptr;
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
			ASSERT(!m_implementation->m_handle);
			LUMIX_DELETE(m_implementation->m_allocator, m_implementation);
		}

		bool Task::create(const char* name)
		{
			HANDLE handle = CreateThread(nullptr, STACK_SIZE, threadFunction, m_implementation, CREATE_SUSPENDED, &m_implementation->m_thread_id);
			if (handle)
			{
				m_implementation->m_exited = false;
				m_implementation->m_thread_name = name;
				m_implementation->m_handle = handle;
			}
			return handle != nullptr;
		}

		bool Task::run()
		{
			m_implementation->m_is_running = true;
			return ::ResumeThread(m_implementation->m_handle) != -1;
		}

		bool Task::destroy()
		{
			while (m_implementation->m_is_running)
			{
				yield();
			}

			::CloseHandle(m_implementation->m_handle);
			m_implementation->m_handle = nullptr;
			return true;
		}

		void Task::setAffinityMask(uint32 affinity_mask)
		{
			m_implementation->m_affinity_mask = affinity_mask;
			if (m_implementation->m_handle)
			{
				::SetThreadIdealProcessor(m_implementation->m_handle, affinity_mask);
			}
		}

		void Task::setPriority(uint32 priority)
		{
			m_implementation->m_priority = priority;
			if (m_implementation->m_handle)
			{
				::SetThreadPriority(m_implementation->m_handle, priority);
			}
		}

		uint32 Task::getAffinityMask() const
		{
			return m_implementation->m_affinity_mask;
		}

		uint32 Task::getPriority() const
		{
			return m_implementation->m_priority;
		}

		uint32 Task::getExitCode() const
		{
			uint32 exit_code = 0xffffFFFF;
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

		IAllocator& Task::getAllocator()
		{
			return m_implementation->m_allocator;
		}

		void Task::forceExit(bool wait)
		{
			m_implementation->m_force_exit = true;

			while (!isFinished() && wait)
			{
				yield();
			}
		}

		void Task::exit(int32 exit_code)
		{
			m_implementation->m_exited = true;
			m_implementation->m_is_running = false;
			::ExitThread(exit_code);
		}

	} // namespace MT
} // namespace Lumix
