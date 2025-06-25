#pragma once


#include "allocator.h"
#include "atomic.h"
#include "core.h"
#include "sync.h"


namespace Lumix
{

struct TagAllocator;
template <typename T> struct Span;

namespace debug
{

struct StackNode;

LUMIX_CORE_API void init(IAllocator& allocator);
LUMIX_CORE_API void shutdown();
LUMIX_CORE_API void debugBreak();
LUMIX_CORE_API void debugOutput(const char* message);
LUMIX_CORE_API void enableFloatingPointTraps(bool enable);

#ifdef _WIN32
struct LUMIX_CORE_API GuardAllocator final : IAllocator {
	void* allocate(size_t size, size_t align) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override { 
		ASSERT(!ptr);
		return allocate(new_size, align);
	}
};
#endif

struct LUMIX_CORE_API AllocationInfo {
	enum Flags : u16{
		NONE = 0,
		IS_VRAM = 0b01 << 1,
		IS_PAGED = 0b10 << 1,
		IS_ARENA = 0b11 << 1,
	};
	AllocationInfo* previous = nullptr;
	AllocationInfo* next = nullptr;
	StackNode* stack_leaf = nullptr;
	TagAllocator* tag = nullptr;
	size_t size = 0;
	u16 align = 0;
	Flags flags = Flags::NONE;
};

LUMIX_CORE_API void registerAlloc(AllocationInfo& info);
LUMIX_CORE_API void resizeAlloc(AllocationInfo& info, u64 new_size);
LUMIX_CORE_API void unregisterAlloc(const AllocationInfo& info);
LUMIX_CORE_API u64 getRegisteredAllocsSize(); // does not include vram allocations
LUMIX_CORE_API void checkLeaks();
LUMIX_CORE_API void checkGuards();
LUMIX_CORE_API const AllocationInfo* lockAllocationInfos();
LUMIX_CORE_API void unlockAllocationInfos();


struct LUMIX_CORE_API Allocator final : IAllocator {
	explicit Allocator(IAllocator& source);
	~Allocator();

	void* allocate(size_t size, size_t align) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override;
	IAllocator* getParent() const override { return &m_source; }

private:
	IAllocator& m_source;
	Mutex m_mutex;
	bool m_is_fill_enabled;
	AtomicI64 m_total_size = 0;
};

} // namespace Debug


LUMIX_CORE_API void enableCrashReporting(bool enable);
LUMIX_CORE_API void installUnhandledExceptionHandler();
LUMIX_CORE_API void clearHardwareBreakpoint(u32 breakpoint_idx);
LUMIX_CORE_API void setHardwareBreakpoint(u32 breakpoint_idx, const void* mem, u32 size);


} // namespace Lumix
