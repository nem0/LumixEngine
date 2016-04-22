#include "lumix.h"
#include "engine/core/mtjd/worker_thread.h"

#include "engine/core/mtjd/manager.h"
#include "engine/core/mtjd/job.h"

#define PROFILE_START
#define PROFILE_STOP

namespace Lumix
{
	namespace MTJD
	{
#if TYPE == MULTI_THREAD

		WorkerTask::WorkerTask(IAllocator& allocator)
			: Task(allocator)
		{
		}

		WorkerTask::~WorkerTask()
		{
		}

		bool WorkerTask::create(const char* name, Manager* manager, Manager::JobTransQueue* trans_queue)
		{
			ASSERT(manager);
			ASSERT(trans_queue);

			m_manager = manager;
			m_trans_queue = trans_queue;

			return Task::create(name);
		}

		int WorkerTask::task()
		{
			ASSERT(m_trans_queue);

			while (!m_trans_queue->isAborted())
			{
				Manager::JobTrans* tr = m_trans_queue->pop(true);
				if (!tr)
					break;

				PROFILE_START("WorkerTask");
				PROFILE_START(tr->data->getJobName());
				tr->data->execute();
				tr->setCompleted();
				PROFILE_STOP(tr->data->getJobName());
				PROFILE_STOP("WorkerTask");

				m_manager->doScheduling();
			}

			return 0;
		}

#endif // TYPE == MULTI_THREAD
	} // namepsace MTJD
} // namepsace Lumix
