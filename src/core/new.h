#pragma once

#include "core/lux.h"

void* operator new		(size_t size);
void* operator new[]	(size_t size);
void* operator new		(size_t size, size_t alignment);
void* operator new[]	(size_t size, size_t alignment);
//todo: exceptions
//void* operator new	(size_t size, const std::nothrow_t&);
//void* operator new[]	(size_t size, const std::nothrow_t&);

void* operator new		(size_t size, const char* file, int line);
void* operator new[]	(size_t size, const char* file, int line);
void* operator new		(size_t size, size_t alignment, const char* file, int line);
void* operator new[]	(size_t size, size_t alignment, const char* file, int line);

void operator delete	(void* p);
void operator delete[]	(void* p);
void operator delete	(void* p, size_t alignment);
void operator delete[]	(void* p, size_t alignment);
//todo: exceptions
//void operator delete	(void* p, const std::nothrow_t&);
//void operator delete[](void* p, const std::nothrow_t&);

void operator delete	(void* p, const char* file, int line);
void operator delete[]	(void* p, const char* file, int line);
void operator delete	(void* p, size_t alignment, const char* file, int line);
void operator delete[]	(void* p, size_t alignment, const char* file, int line);


