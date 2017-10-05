#include "engine/fibers.h"
#include "engine/lumix.h"
#include <ucontext.h>
#include <stdlib.h>

namespace Lumix
{


namespace Fiber
{


void  initThread()
{
}


Handle create(int stack_size, FiberProc proc, void* parameter)
{
	ucontext_t fib;
	getcontext(&fib);
    fib.uc_stack.ss_sp = (::malloc)(stack_size);
    fib.uc_stack.ss_size = stack_size;
    fib.uc_link = 0;
    makecontext(&fib, (void(*)())proc, 1, parameter); 
	return fib;
}


void destroy(Handle fiber)
{
	ASSERT(false);
}


void switchTo(Handle* prev, Handle fiber)
{
	swapcontext(prev, &fiber); 
}


void* getParameter()
{
	ASSERT(false);
	return nullptr;
}


} // namespace Fibers


} // namespace Lumix