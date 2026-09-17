#include "platform.h"
#include <stdlib.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

void* ex_platform_allocate(size_t size, size_t align) {
	if (align < sizeof(void*)) align = sizeof(void*);
#if defined(_MSC_VER)
	return _aligned_malloc(size, align);
#else
	void* ptr = NULL;
	return posix_memalign(&ptr, align, size) == 0 ? ptr : NULL;
#endif
}

void ex_platform_deallocate(void* ptr) {
#if defined(_MSC_VER)
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

#ifdef _WIN32
#include <windows.h>

size_t ex_platform_page_size(void) {
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
}

void* ex_platform_reserve(size_t size) {
	return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool ex_platform_commit(void* address, size_t size) {
	return VirtualAlloc(address, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

void ex_platform_release(void* address, size_t size) {
	(void)size;
	if (address) VirtualFree(address, 0, MEM_RELEASE);
}

static LARGE_INTEGER frequency;
static bool initialized;

double ex_platform_now_ms(void) {
	LARGE_INTEGER counter;
	if (!initialized) {
		QueryPerformanceFrequency(&frequency);
		initialized = true;
	}
	QueryPerformanceCounter(&counter);
	return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
}
#else
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

size_t ex_platform_page_size(void) {
	long value = sysconf(_SC_PAGESIZE);
	return value > 0 ? (size_t)value : 4096u;
}

void* ex_platform_reserve(size_t size) {
	void* address = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return address == MAP_FAILED ? NULL : address;
}

bool ex_platform_commit(void* address, size_t size) {
	return mprotect(address, size, PROT_READ | PROT_WRITE) == 0;
}

void ex_platform_release(void* address, size_t size) {
	if (address) munmap(address, size);
}

double ex_platform_now_ms(void) {
	struct timespec value;
	clock_gettime(CLOCK_MONOTONIC, &value);
	return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}
#endif
