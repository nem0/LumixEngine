#pragma once


namespace Lumix
{

namespace Fiber
{


#ifdef _WIN32
	typedef void(__stdcall *FiberProc)(void*);
#else 
	typedef void (*FiberProc)(void*);
#endif
typedef void* Handle;


Handle init(void* parameter);
Handle create(int stack_size, FiberProc proc, void* parameter);
void switchTo(Handle fiber);
void* getParameter();


} // namespace Fiber


} // namespace Lumix