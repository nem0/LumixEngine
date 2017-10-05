#include "engine/fibers.h"
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


void switchTo(Handle* from, Handle fiber)
{
	SwitchToFiber(fiber);
}


void* getParameter()
{
	return GetFiberData();
}


} // namespace Fibers


} // namespace Lumix