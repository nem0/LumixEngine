#pragma once


#include "engine/allocator.h"
#include "engine/lumix.h"
#include "engine/sync.h"


namespace Lumix
{
namespace Debug
{


LUMIX_ENGINE_API void debugBreak();
LUMIX_ENGINE_API void debugOutput(const char* message);
LUMIX_ENGINE_API void enableFloatingPointTraps(bool enable);


struct StackNode;


struct LUMIX_ENGINE_API StackTree
{
public:
	StackTree();
	~StackTree();

	StackNode* record();
	void printCallstack(StackNode* node);
	static bool getFunction(StackNode* node, Span<char> out, Ref<int> line);
	static StackNode* getParent(StackNode* node);
	static int getPath(StackNode* node, Span<StackNode*> output);
	static void refreshModuleList();

private:
	StackNode* insertChildren(StackNode* node, void** instruction, void** stack);

private:
	StackNode* m_root;
	static i32 s_instances;
};


struct LUMIX_ENGINE_API Allocator final : IAllocator
{
public:
	struct AllocationInfo
	{
		AllocationInfo* previous;
		AllocationInfo* next;
		size_t size;
		StackNode* stack_leaf;
		u16 align;
	};

public:
	explicit Allocator(IAllocator& source);
	~Allocator();

	bool isDebug() const override { return true; }

	void* allocate(size_t size) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t size) override;
	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t size, size_t align) override;
	size_t getTotalSize() const { return m_total_size; }
	void checkGuards();
	void checkLeaks();

	IAllocator& getSourceAllocator() { return m_source; }
	AllocationInfo* getFirstAllocationInfo() const { return m_root; }
	void lock();
	void unlock();

private:
	inline size_t getAllocationOffset();
	inline AllocationInfo* getAllocationInfoFromSystem(void* system_ptr);
	inline AllocationInfo* getAllocationInfoFromUser(void* user_ptr);
	inline u8* getUserFromSystem(void* system_ptr, size_t align);
	inline u8* getSystemFromUser(void* user_ptr);
	inline size_t getNeededMemory(size_t size);
	inline size_t getNeededMemory(size_t size, size_t align);
	inline void* getUserPtrFromAllocationInfo(AllocationInfo* info);

private:
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


void LUMIX_ENGINE_API enableCrashReporting(bool enable);
void LUMIX_ENGINE_API installUnhandledExceptionHandler();


} // namespace Lumix
