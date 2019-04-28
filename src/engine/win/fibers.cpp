#include "engine/fibers.h"
#include "engine/profiler.h"
#include <Windows.h>

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


void switchTo(Handle* from, Handle fiber, SwitchReason reason)
{
	const i32 switch_id = Profiler::beginFiberSwitch(reason);
	SwitchToFiber(fiber);
	Profiler::endFiberSwitch(switch_id);
}


} // namespace Fibers


} // namespace Lumix