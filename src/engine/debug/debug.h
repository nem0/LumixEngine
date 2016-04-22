#pragma once


#include "lumix.h"
#include "engine/core/iallocator.h"
#include "engine/core/mt/sync.h"


namespace Lumix
{
namespace Debug
{


void LUMIX_ENGINE_API debugBreak();
void LUMIX_ENGINE_API debugOutput(const char* message);


class StackNode;


class LUMIX_ENGINE_API StackTree
{
public:
	StackTree();
	~StackTree();

	StackNode* record();
	void printCallstack(StackNode* node);
	static bool getFunction(StackNode* node, char* out, int max_size, int* line);
	static StackNode* getParent(StackNode* node);
	static int getPath(StackNode* node, StackNode** output, int max_size);
	static void refreshModuleList();

private:
	StackNode* insertChildren(StackNode* node, void** instruction, void** stack);

private:
	StackNode* m_root;
	static int32 s_instances;
};


class LUMIX_ENGINE_API Allocator : public IAllocator
{
public:
	class AllocationInfo
	{
	public:
		AllocationInfo* m_previous;
		AllocationInfo* m_next;
		size_t m_size;
		StackNode* m_stack_leaf;
	};

public:
	explicit Allocator(IAllocator& source);
	virtual ~Allocator();

	void* allocate(size_t size) override;
	void deallocate(void* ptr) override;
	void* reallocate(void* ptr, size_t size) override;
	void* allocate_aligned(size_t size, size_t align) override;
	void deallocate_aligned(void* ptr) override;
	void* reallocate_aligned(void* ptr, size_t size, size_t align) override;
	size_t getTotalSize() const { return m_total_size; }
	void checkGuards();

	IAllocator& getSourceAllocator() { return m_source; }
	AllocationInfo* getFirstAllocationInfo() const { return m_root; }
	void lock();
	void unlock();

private:
	inline size_t getAllocationOffset();
	inline AllocationInfo* getAllocationInfoFromSystem(void* system_ptr);
	inline AllocationInfo* getAllocationInfoFromUser(void* user_ptr);
	inline void* getUserFromSystem(void* system_ptr);
	inline void* getSystemFromUser(void* user_ptr);
	inline size_t getNeededMemory(size_t size);
	inline void* getUserPtrFromAllocationInfo(AllocationInfo* info);

private:
	IAllocator& m_source;
	StackTree* m_stack_tree;
	MT::SpinMutex m_mutex;
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
