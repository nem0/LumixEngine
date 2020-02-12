#include "engine/fibers.h"
#include "engine/profiler.h"
#include "engine/win/simple_win.h"

namespace Lumix
{


namespace Fiber
{


void initThread(FiberProc proc, Handle* out)
{
	*out = ConvertThreadToFiber(nullptr);
	proc(nullptr);
}


Handle create(int stack_size, FiberProc proc, void* parameter)
{
	return CreateFiber(stack_size, proc, parameter);
}


void destroy(Handle fiber)
{
	DeleteFiber(fiber);
}


void switchTo(Handle* from, Handle fiber)
{
	Profiler::beforeFiberSwitch();
	SwitchToFiber(fiber);
}


bool isValid(Handle handle)
{
	return handle != INVALID_FIBER;
}


} // namespace Fibers


} // namespace Lumix