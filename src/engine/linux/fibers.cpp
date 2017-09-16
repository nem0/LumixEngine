#include "engine/fibers.h"
#include "engine/lumix.h"

namespace Lumix
{


namespace Fiber
{


bool init(Engine& engine)
{
	ASSERT(false);
}


void shutdown()
{
	ASSERT(false);
}


Handle createFromThread(void* parameter)
{
	ASSERT(false);
	return nullptr;
}


Handle create(int stack_size, FiberProc proc, void* parameter)
{
	ASSERT(false);
	return nullptr;
}


void destroy(Handle fiber)
{
	ASSERT(false);
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