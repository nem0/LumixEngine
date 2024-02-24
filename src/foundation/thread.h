#pragma once

#include "foundation/foundation.h"


namespace Lumix
{

struct LUMIX_FOUNDATION_API Thread {
	explicit Thread(struct IAllocator& allocator);
	virtual ~Thread();

	virtual int task() = 0;

	bool create(const char* name, bool is_extended);
	bool destroy();

	void setAffinityMask(u64 affinity_mask);

	// call only from task's thread
	void sleep(struct Mutex& cs);
	void wakeup();

	bool isRunning() const;
	bool isFinished() const;

protected:
	IAllocator& getAllocator();

private:
	struct ThreadImpl* m_implementation;
};


} // namespace Lumix
