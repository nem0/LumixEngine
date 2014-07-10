#include "core/lumix.h"
#include "core/timer.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace Lumix
{


class TimerImpl : public Timer
{
	public:
		TimerImpl()
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

		float tick()
		{
			LARGE_INTEGER tick;
			QueryPerformanceCounter(&tick);
			float delta = static_cast<float>((double)(tick.QuadPart - m_last_tick.QuadPart) / (double)m_frequency.QuadPart);
			m_last_tick = tick;
			return delta;
		} 

		LARGE_INTEGER m_frequency;
		LARGE_INTEGER m_last_tick;
		LARGE_INTEGER m_first_tick;
};



Timer* Timer::create()
{
	return LUMIX_NEW(TimerImpl);
}


void Timer::destroy(Timer* timer)
{
	LUMIX_DELETE(timer);
}


} // ~namespace Lumix
