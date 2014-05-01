#pragma once

#include "core/MT/Task.h"
#include "core/MT/event.h"
#include "core/MT/semaphore.h"

namespace Lux
{
	namespace MTJD
	{
		class Manager;

		class LUX_CORE_API Scheduler : public MT::Task
		{
		public:
			Scheduler(Manager& manager);
			~Scheduler();

			virtual int task();

			void dataSignal();

		private:
			Scheduler& operator= (const Scheduler& rhs);

			MT::Event	m_data_event;
			MT::Event	m_abort_event;
			Manager&	m_manager;
		};
	} // ~namepsace MTJD
} // ~namepsace Lux