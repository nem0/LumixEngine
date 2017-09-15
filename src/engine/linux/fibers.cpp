#include "engine/fibers.h"
#include <Windows.h>


namespace Lumix
{


namespace Fiber
{


Handle init(void* parameter)
{
	ASSERT(false);
	return nullptr;
}


Handle create(int stack_size, FiberProc proc, void* parameter)
{
	ASSERT(false);
	return nullptr;
}


void switchTo(Handle fiber)
{
	ASSERT(false);
}


void* getParameter()
{
	ASSERT(false);
	return nullptr;
}


} // namespace Fibers


} // namespace Lumix