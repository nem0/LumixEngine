#pragma once


#include "engine/mt/task.h"
#include "engine/mt/lock_free_fixed_queue.h"

#include "engine/mtjd/manager.h"


#if !LUMIX_SINGLE_THREAD()


namespace Lumix
{


class IAllocator;

namespace MTJD
{


class WorkerTask LUMIX_FINAL : public MT::Task
{
public:
	WorkerTask(IAllocator& allocator);
	~WorkerTask();

	bool create(const char* name, Manager* manager, Manager::JobTransQueue* trans_queue);

	virtual int task();

private:
	Manager::JobTransQueue* m_trans_queue;
	Manager* m_manager;
};


} // namepsace MTJD
} // namepsace Lumix


#endif