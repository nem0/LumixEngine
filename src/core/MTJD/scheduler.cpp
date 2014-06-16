#include "core/lumix.h"
#include "core/MTJD/scheduler.h"

#include "core/MTJD/manager.h"

namespace Lumix
{
	namespace MTJD
	{
		Scheduler::Scheduler(Manager& manager)
			: m_data_event(0)
			, m_abort_event(0)
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

				m_manager.doScheduling();
			}

			return 0;
		}

		void Scheduler::dataSignal()
		{
			m_data_event.trigger();
		}
	} // ~namepsace MTJD
} // ~namepsace Lux