#pragma once


#include "engine/lumix.h"


namespace Lumix
{


class LUMIX_ENGINE_API Timer
{
public:
	Timer();

	float tick();
	float getTimeSinceStart();
	float getTimeSinceTick();
	uint64 getRawTimeSinceStart();
	uint64 getFrequency();

private:
	uint64 m_frequency;
	uint64 m_last_tick;
	uint64 m_first_tick;
};


} // namespace Lumix
