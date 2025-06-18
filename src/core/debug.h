#pragma once


#include "allocator.h"
#include "atomic.h"
#include "core.h"
#include "span.h"
#include "sync.h"


namespace Lumix
{

struct TagAllocator;

namespace debug
{


LUMIX_CORE_API void debugBreak();
LUMIX_CORE_API void debugOutput(const char* message);
LUMIX_CORE_API void enableFloatingPointTraps(bool enable);


struct StackNode;


struct LUMIX_CORE_API StackTree
{
public:
	StackTree();
	~StackTree();

	StackNode* record();
	void printCallstack(StackNode* node);
	static bool getFunction(StackNode* node, Span<char> out, int& line);
	static StackNode* getParent(StackNode* node);
	static int getPath(StackNode* node, Span<StackNode*> output);
	static void refreshModuleList();

private:
	StackNode* insertChildren(StackNode* node, void** instruction, void** stack);

private:
	StackNode* m_root;
	static AtomicI32 s_instances;
};

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
		IS_GPU = 1 << 0
	};
	AllocationInfo* previous;
	AllocationInfo* next;
	StackNode* stack_leaf;
	TagAllocator* tag;
	size_t size;
	u16 align;
	Flags flags = Flags::NONE;
};

struct LUMIX_CORE_API Allocator final : IAllocator {
	explicit Allocator(IAllocator& source);
	~Allocator();

	bool isDebug() const override { return true; }

	void* allocate(size_t size, size_t align) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override;
	size_t getTotalSize() const { return m_total_size; }
	void checkGuards();
	void checkLeaks();
	void registerExternal(AllocationInfo& info);
	void unregisterExternal(const AllocationInfo& info);

	IAllocator& getSourceAllocator() { return m_source; }
	AllocationInfo* getFirstAllocationInfo() const { return m_root; }
	void lock();
	void unlock();
	
	IAllocator* getParent() const override { return &m_source; }

private:
	inline size_t getAllocationOffset();
	inline AllocationInfo* getAllocationInfoFromSystem(void* system_ptr);
	inline AllocationInfo* getAllocationInfoFromUser(void* user_ptr);
	inline u8* getUserFromSystem(void* system_ptr, size_t align);
	inline u8* getSystemFromUser(void* user_ptr);
	inline size_t getNeededMemory(size_t size);
	inline size_t getNeededMemory(size_t size, size_t align);
	inline void* getUserPtrFromAllocationInfo(AllocationInfo* info);

	IAllocator& m_source;
	StackTree m_stack_tree;
	Mutex m_mutex;
	AllocationInfo* m_root;
	AllocationInfo m_sentinels[2];
	size_t m_total_size;
	bool m_is_fill_enabled;
	bool m_are_guards_enabled;
};

} // namespace Debug


LUMIX_CORE_API void enableCrashReporting(bool enable);
LUMIX_CORE_API void installUnhandledExceptionHandler();
LUMIX_CORE_API void clearHardwareBreakpoint(u32 breakpoint_idx);
LUMIX_CORE_API void setHardwareBreakpoint(u32 breakpoint_idx, const void* mem, u32 size);


} // namespace Lumix
