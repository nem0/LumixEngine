#include "engine/allocator.h"
#include "engine/crt.h"
#include "engine/mt/sync.h"
#include "engine/mt/atomic.h"
#include "engine/mt/thread.h"
#include "engine/profiler.h"
#include "engine/string.h"


namespace Lumix
{
namespace MT
{


Semaphore::Semaphore(int init_count, int max_count)
{
	ASSERT(false);
    // TODO
}

Semaphore::~Semaphore()
{
	ASSERT(false);
    // TODO
}

void Semaphore::signal()
{
	ASSERT(false);
    // TODO
}

void Semaphore::wait()
{
	ASSERT(false);
    // TODO
}

bool Semaphore::poll()
{
	ASSERT(false);
    // TODO
}


Event::Event(bool manual_reset)
{
	ASSERT(false);
    // TODO

}

Event::~Event()
{
	ASSERT(false);
    // TODO
}

void Event::reset()
{
	ASSERT(false);
    // TODO
}

void Event::trigger()
{
	ASSERT(false);
    // TODO
}

void Event::waitMultiple(Event& event0, Event& event1, u32 timeout_ms)
{
	ASSERT(false);
    // TODO
}

void Event::waitTimeout(u32 timeout_ms)
{
	ASSERT(false);
    // TODO
}

void Event::wait()
{
	ASSERT(false);
    // TODO
}

bool Event::poll()
{
	ASSERT(false);
    // TODO
}


CriticalSection::CriticalSection()
{
	ASSERT(false);
    // TODO
}


CriticalSection::~CriticalSection()
{
	ASSERT(false);
    // TODO
}

void CriticalSection::enter()
{
	ASSERT(false);
    // TODO
}

void CriticalSection::exit()
{
	ASSERT(false);
    // TODO
}


} // namespace MT
} // namespace Lumix
