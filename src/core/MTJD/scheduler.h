#pragma once

#include "core/MT/Task.h"
#include "core/MT/event.h"
#include "core/MT/semaphore.h"

namespace Lumix
{
	namespace MTJD
	{
		class Manager;

		class LUMIX_CORE_API Scheduler : public MT::Task
		{
		public:
			explicit Scheduler(Manager& manager);
			~Scheduler();

			virtual int task() override;

			void dataSignal();

		private:
			Scheduler& operator= (const Scheduler& rhs);

			MT::Event	m_data_event;
			MT::Event	m_abort_event;
			Manager&	m_manager;
		};
	} // ~namepsace MTJD
} // ~namepsace Lumix
