#include "engine/fibers.h"
#include "engine/lumix.h"
#include <ucontext.h>
#include <stdlib.h>

namespace Lumix
{


namespace Fiber
{


thread_local Handle g_finisher;
thread_local ucontext_t g_finisher_obj;


void initThread(FiberProc proc, Handle* out)
{
	g_finisher = &g_finisher_obj;
	*out = create(64*1024, proc, nullptr);
	getcontext(g_finisher);
	(*out)->uc_link = g_finisher;
	switchTo(&g_finisher, *out);
}


Handle create(int stack_size, FiberProc proc, void* parameter)
{
	ucontext_t* fib = new ucontext_t;
	getcontext(fib);
    fib->uc_stack.ss_sp = (::malloc)(stack_size);
    fib->uc_stack.ss_size = stack_size;
    fib->uc_link = 0;
    makecontext(fib, (void(*)())proc, 1, parameter); 
	return fib;
}


void destroy(Handle fiber)
{
	delete fiber;
	ASSERT(false);
}


void switchTo(Handle* prev, Handle fiber)
{
	swapcontext(*prev, fiber); 
}


void* getParameter()
{
	ASSERT(false);
	return nullptr;
}


} // namespace Fibers


} // namespace Lumix