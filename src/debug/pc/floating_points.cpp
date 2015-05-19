#include "debug/floating_points.h"
#include <float.h>

namespace Lumix
{


void enableFloatingPointTraps(bool enable)
{
	unsigned int cw = _control87(0, 0) & MCW_EM;
	if (enable)
	{
		cw &= ~(_EM_OVERFLOW | _EM_ZERODIVIDE | _EM_INVALID | _EM_DENORMAL); // can not enable _EM_INEXACT because it is common in QT
	}
	else
	{
		cw |= _EM_OVERFLOW | _EM_INVALID | _EM_DENORMAL; // can not enable _EM_INEXACT because it is common in QT
	}
	_control87(cw, MCW_EM);
}


} // namespace Lumix