#pragma once


namespace Lumix
{


class Engine;


namespace Fiber
{

#ifdef _WIN32
	typedef void(__stdcall *FiberProc)(void*);
#else 
	typedef void (*FiberProc)(void*);
#endif
typedef void* Handle;
constexpr void* INVALID_FIBER = nullptr;


Handle createFromThread(void* parameter);
Handle create(int stack_size, FiberProc proc, void* parameter);
void destroy(Handle fiber);
void switchTo(Handle fiber);
void* getParameter();


} // namespace Fiber


} // namespace Lumix