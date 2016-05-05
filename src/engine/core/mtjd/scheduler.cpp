#include "engine/lumix.h"
#include "engine/core/mtjd/scheduler.h"
#include "engine/core/mtjd/manager.h"
#include "engine/core/profiler.h"


namespace Lumix
{
	namespace MTJD
	{
		Scheduler::Scheduler(Manager& manager, IAllocator& allocator)
			: MT::Task(allocator)
			, m_data_event()
			, m_abort_event()
			, m_manager(manager)
		{
		}

		Scheduler::~Scheduler()
		{
		}

		int Scheduler::task()
		{
			while (!isForceExit())
			{
				m_data_event.wait();

				PROFILE_BLOCK("Schedule")
				m_manager.doScheduling();
			}

			return 0;
		}

		void Scheduler::dataSignal()
		{
			m_data_event.trigger();
		}
	} // namepsace MTJD
} // namepsace Lumix
