#include "engine/lumix.h"
#include "engine/iallocator.h"
#include "engine/timer.h"
#include <time.h>


namespace Lumix
{


struct TimerImpl : public Timer
{
	explicit TimerImpl(IAllocator& allocator)
		: m_allocator(allocator)
	{
		clock_gettime(CLOCK_REALTIME, &m_last_tick);
		m_first_tick = m_last_tick;
	}


	float getTimeSinceStart() override
	{
		timespec tick;
		clock_gettime(CLOCK_REALTIME, &tick);
		return float(double(tick.tv_sec - m_first_tick.tv_sec) + double(tick.tv_nsec - m_first_tick.tv_nsec) / 1000000000.0);
	}


	u64 getRawTimeSinceStart() override
	{
		timespec tick;
		clock_gettime(CLOCK_REALTIME, &tick);
		return u64(tick.tv_sec - m_first_tick.tv_sec) * 1000000000 + u64(tick.tv_nsec - m_first_tick.tv_nsec);
	}


	u64 getFrequency() override
	{
		return 1000000000;
	}


	float getTimeSinceTick() override
	{
		timespec tick;
		clock_gettime(CLOCK_REALTIME, &tick);
		return float(double(tick.tv_sec - m_last_tick.tv_sec) + double(tick.tv_nsec - m_last_tick.tv_nsec) / 1000000000.0);
	}

	float tick() override
	{
		timespec tick;
		clock_gettime(CLOCK_REALTIME, &tick);
		float delta = float(double(tick.tv_sec - m_last_tick.tv_sec) + double(tick.tv_nsec - m_last_tick.tv_nsec) / 1000000000.0);
		m_last_tick = tick;
		return delta;
	}

	IAllocator& m_allocator;
	timespec m_last_tick;
	timespec m_first_tick;
};


Timer* Timer::create(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, TimerImpl)(allocator);
}


void Timer::destroy(Timer* timer)
{
	if (!timer) return;

	LUMIX_DELETE(static_cast<TimerImpl*>(timer)->m_allocator, timer);
}


} // ~namespace Lumix
