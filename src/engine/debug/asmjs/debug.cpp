#include "engine/debug/debug.h"
#include "engine/core/mt/atomic.h"
#include "engine/core/string.h"
#include "engine/core/system.h"
#include <cstdlib>
#include <cstdio>


static bool g_is_crash_reporting_enabled = false;


namespace Lumix
{


namespace Debug
{


void debugOutput(const char* message)
{
	puts(message);
}


void debugBreak()
{
	abort();
}


int StackTree::s_instances = 0;


class StackNode
{
public:
	~StackNode()
	{
		delete m_next;
		delete m_first_child;
	}

	void* m_instruction;
	StackNode* m_next;
	StackNode* m_first_child;
	StackNode* m_parent;
};


StackTree::StackTree()
{
}


StackTree::~StackTree()
{
}


void StackTree::refreshModuleList()
{
}


int StackTree::getPath(StackNode* node, StackNode** output, int max_size)
{
	return 0;
}


StackNode* StackTree::getParent(StackNode* node)
{
	return nullptr;
}


bool StackTree::getFunction(StackNode* node, char* out, int max_size, int* line)
{
	return false;
}


void StackTree::printCallstack(StackNode* node)
{
}


StackNode* StackTree::insertChildren(StackNode* root_node, void** instruction, void** stack)
{
	return nullptr;
}


StackNode* StackTree::record()
{
	return nullptr;
}


static const uint32 UNINITIALIZED_MEMORY_PATTERN = 0xCD;
static const uint32 FREED_MEMORY_PATTERN = 0xDD;
static const uint32 ALLOCATION_GUARD = 0xFDFDFDFD;


Allocator::Allocator(IAllocator& source)
	: m_source(source)
	, m_root(nullptr)
	, m_mutex(false)
	, m_stack_tree(LUMIX_NEW(m_source, Debug::StackTree))
	, m_total_size(0)
	, m_is_fill_enabled(true)
	, m_are_guards_enabled(true)
{
	m_sentinels[0].m_next = &m_sentinels[1];
	m_sentinels[0].m_previous = nullptr;
	m_sentinels[0].m_stack_leaf = nullptr;
	m_sentinels[0].m_size = 0;

	m_sentinels[1].m_next = nullptr;
	m_sentinels[1].m_previous = &m_sentinels[0];
	m_sentinels[1].m_stack_leaf = nullptr;
	m_sentinels[1].m_size = 0;

	m_root = &m_sentinels[1];
}


Allocator::~Allocator()
{
	AllocationInfo* last_sentinel = &m_sentinels[1];
	if (m_root != last_sentinel)
	{
		debugOutput("Memory leaks detected!\n");
		AllocationInfo* info = m_root;
		while (info != last_sentinel)
		{
			char tmp[2048];
			sprintf(tmp, "\nAllocation size : %zu, memory %p\n", info->m_size, info + sizeof(info));
			debugOutput(tmp);
			m_stack_tree->printCallstack(info->m_stack_leaf);
			info = info->m_next;
		}
		ASSERT(false);
	}
	LUMIX_DELETE(m_source, m_stack_tree);
}


void Allocator::lock()
{
	m_mutex.lock();
}


void Allocator::unlock()
{
	m_mutex.unlock();
}


void Allocator::checkGuards()
{
	if (m_are_guards_enabled) return;

	auto* info = m_root;
	while (info)
	{
		auto user_ptr = getUserPtrFromAllocationInfo(info);
		void* system_ptr = getSystemFromUser(user_ptr);
		ASSERT(*(uint32*)system_ptr == ALLOCATION_GUARD);
		ASSERT(*(uint32*)((uint8*)user_ptr + info->m_size) == ALLOCATION_GUARD);

		info = info->m_next;
	}
}


size_t Allocator::getAllocationOffset()
{
	return sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0);
}


size_t Allocator::getNeededMemory(size_t size)
{
	return size + sizeof(AllocationInfo) +
		   (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) << 1 : 0);
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromSystem(void* system_ptr)
{
	return (AllocationInfo*)(m_are_guards_enabled ? (uint8*)system_ptr + sizeof(ALLOCATION_GUARD)
												  : system_ptr);
}


void* Allocator::getUserPtrFromAllocationInfo(AllocationInfo* info)
{
	return ((uint8*)info + sizeof(AllocationInfo));
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromUser(void* user_ptr)
{
	return (AllocationInfo*)((uint8*)user_ptr - sizeof(AllocationInfo));
}


void* Allocator::getUserFromSystem(void* system_ptr)
{
	return (uint8*)system_ptr + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) +
		   sizeof(AllocationInfo);
}


void* Allocator::getSystemFromUser(void* user_ptr)
{
	return (uint8*)user_ptr - (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) -
		   sizeof(AllocationInfo);
}


void* Allocator::reallocate(void* user_ptr, size_t size)
{
#ifndef _DEBUG
	return m_source.reallocate(user_ptr, size);
#else
	if (user_ptr == nullptr) return allocate(size);
	if (size == 0) return nullptr;

	void* new_data = allocate(size);
	if (!new_data) return nullptr;

	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	copyMemory(new_data, user_ptr, info->m_size < size ? info->m_size : size);

	deallocate(user_ptr);

	return new_data;
#endif
}


void* Allocator::allocate_aligned(size_t size, size_t align)
{
	return m_source.allocate_aligned(size, align);
}


void Allocator::deallocate_aligned(void* ptr)
{
	m_source.deallocate_aligned(ptr);
}


void* Allocator::reallocate_aligned(void* ptr, size_t size, size_t align)
{
	return m_source.reallocate_aligned(ptr, size, align);
}


void* Allocator::allocate(size_t size)
{
#ifndef _DEBUG
	return m_source.allocate(size);
#else
	void* system_ptr;
	AllocationInfo* info;
	size_t system_size = getNeededMemory(size);
	{
		MT::SpinLock lock(m_mutex);
		system_ptr = m_source.allocate(system_size);
		info = new (NewPlaceholder(), getAllocationInfoFromSystem(system_ptr)) AllocationInfo();

		info->m_previous = m_root->m_previous;
		m_root->m_previous->m_next = info;

		info->m_next = m_root;
		m_root->m_previous = info;

		m_root = info;

		m_total_size += size;
	} // because of the SpinLock

	void* user_ptr = getUserFromSystem(system_ptr);
	info->m_stack_leaf = m_stack_tree->record();
	info->m_size = size;
	if (m_is_fill_enabled)
	{
		memset(user_ptr, UNINITIALIZED_MEMORY_PATTERN, size);
	}

	if (m_are_guards_enabled)
	{
		*(uint32*)system_ptr = ALLOCATION_GUARD;
		*(uint32*)((uint8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) = ALLOCATION_GUARD;
	}

	return user_ptr;
#endif
}

void Allocator::deallocate(void* user_ptr)
{
#ifndef _DEBUG
	m_source.deallocate(user_ptr);
#else
	if (user_ptr)
	{
		AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
		void* system_ptr = getSystemFromUser(user_ptr);
		if (m_is_fill_enabled)
		{
			memset(user_ptr, FREED_MEMORY_PATTERN, info->m_size);
		}

		if (m_are_guards_enabled)
		{
			ASSERT(*(uint32*)system_ptr == ALLOCATION_GUARD);
			ASSERT(*(uint32*)((uint8*)user_ptr + info->m_size) == ALLOCATION_GUARD);
		}

		{
			MT::SpinLock lock(m_mutex);
			if (info == m_root)
			{
				m_root = info->m_next;
			}
			info->m_previous->m_next = info->m_next;
			info->m_next->m_previous = info->m_previous;

			m_total_size -= info->m_size;
		} // because of the SpinLock

		info->~AllocationInfo();

		m_source.deallocate((void*)system_ptr);
	}
#endif
}


} // namespace Debug



void enableCrashReporting(bool enable)
{
	ASSERT(!enable); // not supported on asmjs
	g_is_crash_reporting_enabled = false;
}


void installUnhandledExceptionHandler()
{
}


} // namespace Lumix
