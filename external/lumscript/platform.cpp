#include "platform.h"

#ifdef _WIN32
#include <windows.h>

size_t ls_platform_page_size(void) {
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
}

void* ls_platform_reserve(size_t size) {
	return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool ls_platform_commit(void* address, size_t size) {
	return VirtualAlloc(address, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

void ls_platform_release(void* address, size_t size) {
	(void)size;
	if (address) VirtualFree(address, 0, MEM_RELEASE);
}

static LARGE_INTEGER frequency;
static bool initialized;

double ls_platform_now_ms(void) {
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

size_t ls_platform_page_size(void) {
	long value = sysconf(_SC_PAGESIZE);
	return value > 0 ? (size_t)value : 4096u;
}

void* ls_platform_reserve(size_t size) {
	void* address = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return address == MAP_FAILED ? NULL : address;
}

bool ls_platform_commit(void* address, size_t size) {
	return mprotect(address, size, PROT_READ | PROT_WRITE) == 0;
}

void ls_platform_release(void* address, size_t size) {
	if (address) munmap(address, size);
}

double ls_platform_now_ms(void) {
	struct timespec value;
	clock_gettime(CLOCK_MONOTONIC, &value);
	return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}
#endif
