#include "core/new.h"
#include "core/new_macros.h"

#include <new>

void* operator new (size_t size)
{ 
	return Lumix::dll_lumix_new(size, "unknown", 0); 
}

void* operator new[] (size_t size)
{ 
	return Lumix::dll_lumix_new(size, "unknown", 0); 
}

void* operator new (size_t size, size_t alignment) 
{ 
	return Lumix::dll_lumix_new_aligned(size, alignment, "unknown", 0); 
}

void* operator new[] (size_t size, size_t alignment)
{ 
	return Lumix::dll_lumix_new_aligned(size, alignment, "unknown", 0); 
}

//void* operator new (size_t size, const std::nothrow_t&)						{ return Lumix::dll_lumix_new(size, "unknown" ,0, "", ""); }
//void* operator new[] (size_t size, const std::nothrow_t&)						{ return Lumix::dll_lumix_new(size, "unknown" ,0, "", ""); }

void* operator new (size_t size, const char* file, int line)
{ 
	return Lumix::dll_lumix_new(size, file, line); 
}

void* operator new[] (size_t size, const char* file, int line)
{ 
	return Lumix::dll_lumix_new(size, file, line); 
}

void* operator new (size_t size, size_t alignment, const char* file, int line)
{ 
	return Lumix::dll_lumix_new_aligned(size, alignment, file, line); 
}

void* operator new[] (size_t size, size_t alignment, const char* file, int line)
{ 
	return Lumix::dll_lumix_new_aligned(size, alignment, file, line); 
}

void operator delete (void* p)
{ 
	Lumix::dll_lumix_delete(p); 
}

void operator delete[] (void* p)
{ 
	Lumix::dll_lumix_delete(p); 
}

void operator delete (void* p, size_t)
{ 
	Lumix::dll_lumix_delete_aligned(p); 
}

void operator delete[]	(void* p, size_t) 
{ 
	Lumix::dll_lumix_delete_aligned(p); 
}

//void operator delete	(void* p, const std::nothrow_t&)							{ lumix_delete(p); }
//void operator delete[](void* p, const std::nothrow_t&)							{ lumix_delete(p); }

void operator delete (void* p, const char*, int)
{ 
	Lumix::dll_lumix_delete(p); 
}

void operator delete[] (void* p, const char*, int)
{ 
	Lumix::dll_lumix_delete(p); 
}

void operator delete (void* p, size_t, const char*, int)
{ 
	Lumix::dll_lumix_delete_aligned(p); 
}

void operator delete[] (void* p, size_t, const char*, int)
{ 
	Lumix::dll_lumix_delete_aligned(p); 
}
