#include "engine/lumix.h"
#include "engine/core/mt/thread.h"
#include <SDL_cpuinfo.h>
#include <SDL_timer.h>


namespace Lumix
{
namespace MT
{


void sleep(uint32 milliseconds)
{
	SDL_Delay(milliseconds);
}

uint32 getCPUsCount()
{
	return 1;
}

uint32 getCurrentThreadID()
{
	return 0;
}

uint32 getProccessAffinityMask()
{
	return 0;
}


void setThreadName(uint32 /*thread_id*/, const char* /*thread_name*/)
{
	ASSERT(false);
}


} //! namespace MT
} //! namespace Lumix
