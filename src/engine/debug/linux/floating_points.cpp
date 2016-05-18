#include "engine/debug/floating_points.h"
#include <fenv.h>
#include <float.h>


namespace Lumix
{


void enableFloatingPointTraps(bool enable)
{
	static const int FLAGS = FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW;
	if(enable)
	{
			feenableexcept(FLAGS);
	}
	else
	{
			fedisableexcept(FLAGS);
	}
}


} // namespace Lumix