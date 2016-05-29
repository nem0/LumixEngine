#include "engine/lumix.h"
#include "engine/iallocator.h"
#include "engine/timer.h"
#include <SDL.h>


namespace Lumix
{


Timer::Timer()
{
	m_frequency = SDL_GetPerformanceFrequency();
	m_last_tick = SDL_GetPerformanceCounter();
	m_first_tick = m_last_tick;
}


float Timer::getTimeSinceStart()
{
	Uint64 tick = SDL_GetPerformanceCounter();
	float delta = float((double)(tick - m_first_tick) / (double)m_frequency);
	return delta;
}


uint64 Timer::getRawTimeSinceStart()
{
	Uint64 tick = SDL_GetPerformanceCounter();
	return tick - m_first_tick;
}


uint64 Timer::getFrequency()
{
	return m_frequency;
}


float Timer::getTimeSinceTick()
{
	Uint64 tick = SDL_GetPerformanceCounter();
	float delta = float((double)(tick - m_last_tick) / (double)m_frequency);
	return delta;
}

float Timer::tick()
{
	Uint64 tick = SDL_GetPerformanceCounter();
	float delta = float((double)(tick - m_last_tick) / (double)m_frequency);
	m_last_tick = tick;
	return delta;
}


} // namespace Lumix
