#include "engine/lumix.h"
#include "engine/core/iallocator.h"
#include "engine/core/timer.h"
#include <SDL_timer.h>


namespace Lumix
{


struct TimerImpl : public Timer
{
	explicit TimerImpl(IAllocator& allocator)
		: m_allocator(allocator)
	{
		m_frequency = SDL_GetPerformanceFrequency();
		m_last_tick = SDL_GetPerformanceCounter();
		m_first_tick = m_last_tick;
	}


	float getTimeSinceStart() override
	{
		auto tick = SDL_GetPerformanceCounter();
		return static_cast<float>((double)(tick - m_first_tick) / (double)m_frequency);
	}


	uint64 getRawTimeSinceStart() override
	{
		auto tick = SDL_GetPerformanceCounter();
		return tick - m_first_tick;
	}


	uint64 getFrequency() override
	{
		return m_frequency;
	}


	float getTimeSinceTick() override
	{
		auto tick = SDL_GetPerformanceCounter();
		return static_cast<float>((double)(tick - m_last_tick) / (double)m_frequency);
	}

	float tick() override
	{
		auto tick = SDL_GetPerformanceCounter();
		float delta = static_cast<float>((double)(tick - m_last_tick) / (double)m_frequency);
		m_last_tick = tick;
		return delta;
	}

	IAllocator& m_allocator;
	Uint64 m_frequency;
	Uint64 m_last_tick;
	Uint64 m_first_tick;
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
