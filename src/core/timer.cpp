#include "core/timer.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace Lux
{


class TimerImpl : public Timer
{
	public:
		TimerImpl()
		{
			QueryPerformanceFrequency(&m_frequency);
			QueryPerformanceCounter(&m_last_tick);
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
};



Timer* Timer::create()
{
	return LUX_NEW(TimerImpl);
}


void Timer::destroy(Timer* timer)
{
	LUX_DELETE(timer);
}


} // ~namespace Lux