#include "engine/debug.h"
#include "engine/mt/atomic.h"
#include "engine/string.h"
#include <cstdlib>
#include <cstring>
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


int StackTree::getPath(StackNode* node, Span<StackNode*> output)
{
	return 0;
}


StackNode* StackTree::getParent(StackNode* node)
{
	return nullptr;
}


bool StackTree::getFunction(StackNode* node, Span<char> out, Ref<int> line)
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


static const u32 UNINITIALIZED_MEMORY_PATTERN = 0xCD;
static const u32 FREED_MEMORY_PATTERN = 0xDD;
static const u32 ALLOCATION_GUARD = 0xFDFDFDFD;



Allocator::Allocator(IAllocator& source)
	: m_source(source)
	, m_root(nullptr)
	, m_total_size(0)
	, m_is_fill_enabled(true)
	, m_are_guards_enabled(true)
{
	m_sentinels[0].next = &m_sentinels[1];
	m_sentinels[0].previous = nullptr;
	m_sentinels[0].stack_leaf = nullptr;
	m_sentinels[0].size = 0;
	m_sentinels[0].align = 0;

	m_sentinels[1].next = nullptr;
	m_sentinels[1].previous = &m_sentinels[0];
	m_sentinels[1].stack_leaf = nullptr;
	m_sentinels[1].size = 0;
	m_sentinels[1].align = 0;

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
			sprintf(tmp, "\nAllocation size : %zu, memory %p\n", info->size, info + sizeof(info));
			debugOutput(tmp);
			m_stack_tree.printCallstack(info->stack_leaf);
			info = info->next;
		}
		ASSERT(false);
	}
}


void Allocator::lock()
{
	m_mutex.enter();
}


void Allocator::unlock()
{ 
	m_mutex.exit();
}


void Allocator::checkGuards()
{
	if (m_are_guards_enabled) return;

	auto* info = m_root;
	while (info)
	{
		auto user_ptr = getUserPtrFromAllocationInfo(info);
		void* system_ptr = getSystemFromUser(user_ptr);
		ASSERT(*(u32*)system_ptr == ALLOCATION_GUARD);
		ASSERT(*(u32*)((u8*)user_ptr + info->size) == ALLOCATION_GUARD);

		info = info->next;
	}
}


size_t Allocator::getAllocationOffset()
{
	return sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0);
}


size_t Allocator::getNeededMemory(size_t size)
{
	return size + sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) << 1 : 0);
}


size_t Allocator::getNeededMemory(size_t size, size_t align)
{
	return size + sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) << 1 : 0) +
		align;
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromSystem(void* system_ptr)
{
	return (AllocationInfo*)(m_are_guards_enabled ? (u8*)system_ptr + sizeof(ALLOCATION_GUARD)
		: system_ptr);
}


void* Allocator::getUserPtrFromAllocationInfo(AllocationInfo* info)
{
	return ((u8*)info + sizeof(AllocationInfo));
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromUser(void* user_ptr)
{
	return (AllocationInfo*)((u8*)user_ptr - sizeof(AllocationInfo));
}


u8* Allocator::getUserFromSystem(void* system_ptr, size_t align)
{
	size_t diff = (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) + sizeof(AllocationInfo);

	if (align) diff += (align - diff % align) % align;
	return (u8*)system_ptr + diff;
}


u8* Allocator::getSystemFromUser(void* user_ptr)
{
	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	size_t diff = (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) + sizeof(AllocationInfo);
	if (info->align) diff += (info->align - diff % info->align) % info->align;
	return (u8*)user_ptr - diff;
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
	copyMemory(new_data, user_ptr, info->size < size ? info->size : size);

	deallocate(user_ptr);

	return new_data;
#endif
}


void* Allocator::allocate_aligned(size_t size, size_t align)
{
#ifndef _DEBUG
	return m_source.allocate_aligned(size, align);
#else
	void* system_ptr;
	AllocationInfo* info;
	u8* user_ptr;

	size_t system_size = getNeededMemory(size, align);
	{
		MT::SpinLock lock(m_mutex);
		system_ptr = m_source.allocate_aligned(system_size, align);
		user_ptr = getUserFromSystem(system_ptr, align);
		info = new (NewPlaceholder(), getAllocationInfoFromUser(user_ptr)) AllocationInfo();

		info->previous = m_root->previous;
		m_root->previous->next = info;

		info->next = m_root;
		m_root->previous = info;

		m_root = info;

		m_total_size += size;
	} // because of the SpinLock

	info->align = u16(align);
	info->stack_leaf = m_stack_tree.record();
	info->size = size;
	if (m_is_fill_enabled)
	{
		memset(user_ptr, UNINITIALIZED_MEMORY_PATTERN, size);
	}

	if (m_are_guards_enabled)
	{
		*(u32*)system_ptr = ALLOCATION_GUARD;
		*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) = ALLOCATION_GUARD;
	}

	return user_ptr;
#endif
}


void Allocator::deallocate_aligned(void* user_ptr)
{
#ifndef _DEBUG
	m_source.deallocate_aligned(user_ptr);
#else
	if (user_ptr)
	{
		AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
		void* system_ptr = getSystemFromUser(user_ptr);
		if (m_is_fill_enabled)
		{
			memset(user_ptr, FREED_MEMORY_PATTERN, info->size);
		}

		if (m_are_guards_enabled)
		{
			ASSERT(*(u32*)system_ptr == ALLOCATION_GUARD);
			size_t system_size = getNeededMemory(info->size, info->align);
			ASSERT(*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) == ALLOCATION_GUARD);
		}

		{
			MT::SpinLock lock(m_mutex);
			if (info == m_root)
			{
				m_root = info->next;
			}
			info->previous->next = info->next;
			info->next->previous = info->previous;

			m_total_size -= info->size;
		} // because of the SpinLock

		info->~AllocationInfo();

		m_source.deallocate_aligned((void*)system_ptr);
	}
#endif
}


void* Allocator::reallocate_aligned(void* user_ptr, size_t size, size_t align)
{
#ifndef _DEBUG
	return m_source.reallocate_aligned(user_ptr, size, align);
#else
	if (user_ptr == nullptr) return allocate_aligned(size, align);
	if (size == 0) return nullptr;

	void* new_data = allocate_aligned(size, align);
	if (!new_data) return nullptr;

	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	copyMemory(new_data, user_ptr, info->size < size ? info->size : size);

	deallocate_aligned(user_ptr);

	return new_data;
#endif
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

		info->previous = m_root->previous;
		m_root->previous->next = info;

		info->next = m_root;
		m_root->previous = info;

		m_root = info;

		m_total_size += size;
	} // because of the SpinLock

	void* user_ptr = getUserFromSystem(system_ptr, 0);
	info->stack_leaf = m_stack_tree.record();
	info->size = size;
	info->align = 0;
	if (m_is_fill_enabled)
	{
		memset(user_ptr, UNINITIALIZED_MEMORY_PATTERN, size);
	}

	if (m_are_guards_enabled)
	{
		*(u32*)system_ptr = ALLOCATION_GUARD;
		*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) = ALLOCATION_GUARD;
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
			memset(user_ptr, FREED_MEMORY_PATTERN, info->size);
		}

		if (m_are_guards_enabled)
		{
			ASSERT(*(u32*)system_ptr == ALLOCATION_GUARD);
			size_t system_size = getNeededMemory(info->size);
			ASSERT(*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) == ALLOCATION_GUARD);
		}

		{
			MT::SpinLock lock(m_mutex);
			if (info == m_root)
			{
				m_root = info->next;
			}
			info->previous->next = info->next;
			info->next->previous = info->previous;

			m_total_size -= info->size;
		} // because of the SpinLock

		info->~AllocationInfo();

		m_source.deallocate((void*)system_ptr);
	}
#endif
}


} // namespace Debug



void enableCrashReporting(bool enable)
{
	g_is_crash_reporting_enabled = false;
}


void installUnhandledExceptionHandler()
{
}


} // namespace Lumix
