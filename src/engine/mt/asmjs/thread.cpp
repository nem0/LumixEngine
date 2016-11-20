#include "engine/lumix.h"
#include "engine/mt/thread.h"
#include <SDL_cpuinfo.h>
#include <SDL_timer.h>


namespace Lumix
{
namespace MT
{


void sleep(u32 milliseconds)
{
	SDL_Delay(milliseconds);
}

u32 getCPUsCount()
{
	return 1;
}

u32 getCurrentThreadID()
{
	return 0;
}

u32 getProccessAffinityMask()
{
	return 0;
}


void setThreadName(u32 /*thread_id*/, const char* /*thread_name*/)
{
	ASSERT(false);
}


} //! namespace MT
} //! namespace Lumix
