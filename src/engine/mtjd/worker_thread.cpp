#include "engine/lumix.h"
#include "engine/mtjd/worker_thread.h"
#include "engine/mtjd/manager.h"
#include "engine/mtjd/job.h"
#include "engine/profiler.h"

namespace Lumix
{
	namespace MTJD
	{
#if !LUMIX_SINGLE_THREAD()

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

				Profiler::beginBlock("WorkerTask");
				Profiler::beginBlock(tr->data->getJobName());
				tr->data->execute();
				tr->setCompleted();
				Profiler::endBlock();
				Profiler::endBlock();

				m_manager->doScheduling();
			}

			return 0;
		}

#endif
	} // namepsace MTJD
} // namepsace Lumix
