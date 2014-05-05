#pragma once

#include "core/lux.h"

void* operator new		(size_t size);
void* operator new[]	(size_t size);
void* operator new		(size_t size, size_t alignment);
void* operator new[]	(size_t size, size_t alignment);

void* operator new		(size_t size, const char* file, int line);
void* operator new[]	(size_t size, const char* file, int line);
void* operator new		(size_t size, size_t alignment, const char* file, int line);
void* operator new[]	(size_t size, size_t alignment, const char* file, int line);

void operator delete	(void* p);
void operator delete[]	(void* p);
void operator delete	(void* p, size_t alignment);
void operator delete[]	(void* p, size_t alignment);

void operator delete	(void* p, const char* file, int line);
void operator delete[]	(void* p, const char* file, int line);
void operator delete	(void* p, size_t alignment, const char* file, int line);
void operator delete[]	(void* p, size_t alignment, const char* file, int line);

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE
LUX_FORCE_INLINE void* operator new(size_t, void *ptr) { return (ptr); }
LUX_FORCE_INLINE void  operator delete(void *, void *) { return; }
#endif // __PLACEMENT_NEW_INLINE
