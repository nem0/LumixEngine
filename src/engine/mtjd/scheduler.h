#pragma once

#include "engine/mt/task.h"
#include "engine/mt/sync.h"


#if !LUMIX_SINGLE_THREAD()


namespace Lumix
{
namespace MTJD
{


class Manager;


class LUMIX_ENGINE_API Scheduler LUMIX_FINAL : public MT::Task
{
public:
	Scheduler(Manager& manager, IAllocator& allocator);
	~Scheduler();

	int task() override;

	void dataSignal();

private:
	Scheduler(const Scheduler&);
	Scheduler& operator=(const Scheduler& rhs);

	MT::Event m_data_event;
	MT::Event m_abort_event;
	Manager& m_manager;
};


} // namepsace MTJD
} // namepsace Lumix


#endif