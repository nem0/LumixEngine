#pragma once


#include "core/MT/task.h"
#include "core/MT/transaction_queue.h"

#include "core/MTJD/manager.h"

namespace Lux
{
	namespace MTJD
	{
		class WorkerTask : public MT::Task
		{
		public:
			WorkerTask();
			~WorkerTask();

			bool create(const char* name, Manager* manager);

			virtual int task();

			void setTransQueue(Manager::JobTransQueue* trans_queue) { m_trans_queue = trans_queue; }

		private:
			Manager::JobTransQueue*	m_trans_queue;
			Manager* m_manager;
		};
	} // ~namepsace MTJD
} // ~namepsace Lux
