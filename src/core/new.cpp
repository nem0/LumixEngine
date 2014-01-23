#include "core/new.h"
#include "core/new_macros.h"

#include <new>

void* operator new (size_t size)
{ 
	return Lux::dll_lux_new(size, "unknown", 0); 
}

void* operator new[] (size_t size)
{ 
	return Lux::dll_lux_new(size, "unknown", 0); 
}

void* operator new (size_t size, size_t alignment) 
{ 
	return Lux::dll_lux_new_aligned(size, alignment, "unknown", 0); 
}

void* operator new[] (size_t size, size_t alignment)
{ 
	return Lux::dll_lux_new_aligned(size, alignment, "unknown", 0); 
}

//void* operator new (size_t size, const std::nothrow_t&)						{ return Lux::dll_lux_new(size, "unknown" ,0, "", ""); }
//void* operator new[] (size_t size, const std::nothrow_t&)						{ return Lux::dll_lux_new(size, "unknown" ,0, "", ""); }

void* operator new (size_t size, const char* file, int line)
{ 
	return Lux::dll_lux_new(size, file, line); 
}

void* operator new[] (size_t size, const char* file, int line)
{ 
	return Lux::dll_lux_new(size, file, line); 
}

void* operator new (size_t size, size_t alignment, const char* file, int line)
{ 
	return Lux::dll_lux_new_aligned(size, alignment, file, line); 
}

void* operator new[] (size_t size, size_t alignment, const char* file, int line)
{ 
	return Lux::dll_lux_new_aligned(size, alignment, file, line); 
}

void operator delete (void* p)
{ 
	Lux::dll_lux_delete(p); 
}

void operator delete[] (void* p)
{ 
	Lux::dll_lux_delete(p); 
}

void operator delete (void* p, size_t alignment)
{ 
	Lux::dll_lux_delete_aligned(p); 
}

void operator delete[]	(void* p, size_t alignment) 
{ 
	Lux::dll_lux_delete_aligned(p); 
}

//void operator delete	(void* p, const std::nothrow_t&)							{ lux_delete(p); }
//void operator delete[](void* p, const std::nothrow_t&)							{ lux_delete(p); }

void operator delete (void* p, const char* file, int line)
{ 
	Lux::dll_lux_delete(p); 
}

void operator delete[] (void* p, const char* file, int line)
{ 
	Lux::dll_lux_delete(p); 
}

void operator delete (void* p, size_t alignment, const char* file, int line)
{ 
	Lux::dll_lux_delete_aligned(p); 
}

void operator delete[] (void* p, size_t alignment, const char* file, int line)
{ 
	Lux::dll_lux_delete_aligned(p); 
}
