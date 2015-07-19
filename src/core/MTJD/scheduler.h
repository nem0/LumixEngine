#pragma once

#include "core/mt/Task.h"
#include "core/mt/event.h"
#include "core/mt/semaphore.h"

namespace Lumix
{
	namespace MTJD
	{
		class Manager;

		class LUMIX_ENGINE_API Scheduler : public MT::Task
		{
		public:
			Scheduler(Manager& manager, IAllocator& allocator);
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
