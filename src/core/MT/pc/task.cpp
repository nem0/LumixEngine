#include "core/lumix.h"
#include "core/iallocator.h"
#include "core/mt/task.h"
#include "core/mt/thread.h"
#include <Windows.h>

namespace Lumix
{
	namespace MT
	{
		const uint32_t STACK_SIZE = 0x8000;

		struct TaskImpl
		{
			TaskImpl(IAllocator& allocator)
				: m_allocator(allocator)
			{ }

			IAllocator& m_allocator;
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
			setThreadName(impl->m_thread_id, impl->m_thread_name);
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
			TaskImpl* impl = allocator.newObject<TaskImpl>(allocator);
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
			m_implementation->m_allocator.deleteObject(m_implementation);
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

		void Task::setAffinityMask(uint32_t affinity_mask)
		{
			m_implementation->m_affinity_mask = affinity_mask;
			if (m_implementation->m_handle)
			{
				::SetThreadIdealProcessor(m_implementation->m_handle, affinity_mask);
			}
		}

		void Task::setPriority(uint32_t priority)
		{
			m_implementation->m_priority = priority;
			if (m_implementation->m_handle)
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

		void Task::exit(int32_t exit_code)
		{
			m_implementation->m_exited = true;
			m_implementation->m_is_running = false;
			::ExitThread(exit_code);
		}

		//namespace Logger
		//{
		//	Event g_events[BUFFER_SIZE];
		//	LONG g_pos = -1;
		//}
	} // ~namespace MT
} // ~namespace Lumix
