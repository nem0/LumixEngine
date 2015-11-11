#include "lumix.h"
#include "core/iallocator.h"
#include "core/timer.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace Lumix
{


class TimerImpl : public Timer
{
	public:
		TimerImpl(IAllocator& allocator)
			: m_allocator(allocator)
		{
			QueryPerformanceFrequency(&m_frequency);
			QueryPerformanceCounter(&m_last_tick);
			m_first_tick = m_last_tick;
		}

		float getTimeSinceStart()
		{
			LARGE_INTEGER tick;
			QueryPerformanceCounter(&tick);
			float delta = static_cast<float>((double)(tick.QuadPart - m_first_tick.QuadPart) / (double)m_frequency.QuadPart);
			return delta;
		}

		float getTimeSinceTick() override
		{
			LARGE_INTEGER tick;
			QueryPerformanceCounter(&tick);
			float delta = static_cast<float>(
				(double)(tick.QuadPart - m_last_tick.QuadPart) / (double)m_frequency.QuadPart);
			return delta;
		}

		float tick() override
		{
			LARGE_INTEGER tick;
			QueryPerformanceCounter(&tick);
			float delta = static_cast<float>(
				(double)(tick.QuadPart - m_last_tick.QuadPart) / (double)m_frequency.QuadPart);
			m_last_tick = tick;
			return delta;
		} 

		IAllocator& m_allocator;
		LARGE_INTEGER m_frequency;
		LARGE_INTEGER m_last_tick;
		LARGE_INTEGER m_first_tick;
};



Timer* Timer::create(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, TimerImpl)(allocator);
}


void Timer::destroy(Timer* timer)
{
	LUMIX_DELETE(static_cast<TimerImpl*>(timer)->m_allocator, timer);
}


} // ~namespace Lumix
