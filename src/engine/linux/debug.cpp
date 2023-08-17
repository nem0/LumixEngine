#include "engine/allocators.h"
#include "engine/debug.h"
#include "engine/atomic.h"
#include "engine/string.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>

static bool g_is_crash_reporting_enabled = false;


namespace Lumix {


namespace debug {


static Lumix::DefaultAllocator stack_node_allocator;


void debugOutput(const char* message) {
	printf("%s", message);
}


void debugBreak() {
	abort();
}


int StackTree::s_instances = 0;


struct StackNode {
	~StackNode() {
		delete m_next;
		delete m_first_child;
	}

	void* m_instruction;
	StackNode* m_next;
	StackNode* m_first_child;
	StackNode* m_parent;
};


StackTree::StackTree()
	: m_root(nullptr)
{}

StackTree::~StackTree() {}

void StackTree::refreshModuleList() {}


int StackTree::getPath(StackNode* node, Span<StackNode*> output) {
	u32 i = 0;
	while (i < output.length() && node) {
		output[i] = node;
		i++;
		node = node->m_parent;
	}
	return i;
}


StackNode* StackTree::getParent(StackNode* node) {
	return node ? node->m_parent : nullptr;
}


bool StackTree::getFunction(StackNode* node, Span<char> out, int& line) {
	char** str = backtrace_symbols(&node->m_instruction, 1);
	if (!str) return false;

	copyString(out, *str);
	free(str);
	line = -1;
	return true;
}


void StackTree::printCallstack(StackNode* node) {
	char** str = backtrace_symbols(&node->m_instruction, 1);
	if (str) {
		printf("%s", *str);
		free(str);
	}
}


StackNode* StackTree::insertChildren(StackNode* root_node, void** instruction, void** stack) {
	StackNode* node = root_node;
	while (instruction >= stack) {
		StackNode* new_node = LUMIX_NEW(stack_node_allocator, StackNode)();
		node->m_first_child = new_node;
		new_node->m_parent = node;
		new_node->m_next = nullptr;
		new_node->m_first_child = nullptr;
		new_node->m_instruction = *instruction;
		node = new_node;
		--instruction;
	}
	return node;
}

StackNode* StackTree::record() {
	static const int frames_to_capture = 256;
	void* stack[frames_to_capture];
	const int captured_frames_count = backtrace(stack, frames_to_capture);

	void** ptr = stack + captured_frames_count - 1;
	if (!m_root) {
		m_root = LUMIX_NEW(stack_node_allocator, StackNode)();
		m_root->m_instruction = *ptr;
		m_root->m_first_child = nullptr;
		m_root->m_next = nullptr;
		m_root->m_parent = nullptr;
		--ptr;
		return insertChildren(m_root, ptr, stack);
	}

	StackNode* node = m_root;
	while (ptr >= stack) {
		while (node->m_instruction != *ptr && node->m_next) {
			node = node->m_next;
		}
		if (node->m_instruction != *ptr) {
			node->m_next = LUMIX_NEW(stack_node_allocator, StackNode);
			node->m_next->m_parent = node->m_parent;
			node->m_next->m_instruction = *ptr;
			node->m_next->m_next = nullptr;
			node->m_next->m_first_child = nullptr;
			--ptr;
			return insertChildren(node->m_next, ptr, stack);
		}

		if (node->m_first_child) {
			--ptr;
			node = node->m_first_child;
		} else if (ptr != stack) {
			--ptr;
			return insertChildren(node, ptr, stack);
		} else {
			return node;
		}
	}

	return node;
}

static const u32 UNINITIALIZED_MEMORY_PATTERN = 0xCD;
static const u32 FREED_MEMORY_PATTERN = 0xDD;
static const u32 ALLOCATION_GUARD = 0xFDFDFDFD;


Allocator::Allocator(IAllocator& source)
	: m_source(source)
	, m_root(nullptr)
	, m_total_size(0)
	, m_is_fill_enabled(true)
	, m_are_guards_enabled(true) {
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


Allocator::~Allocator() {
	AllocationInfo* last_sentinel = &m_sentinels[1];
	if (m_root != last_sentinel) {
		debugOutput("Memory leaks detected!\n");
		AllocationInfo* info = m_root;
		while (info != last_sentinel) {
			char tmp[2048];
			sprintf(tmp, "\nAllocation size : %zu, memory %p\n", info->size, info + sizeof(info)); //-V568
			debugOutput(tmp);
			m_stack_tree.printCallstack(info->stack_leaf);
			info = info->next;
		}
		ASSERT(false);
	}
}


void Allocator::lock() {
	m_mutex.enter();
}


void Allocator::unlock() {
	m_mutex.exit();
}


void Allocator::checkGuards() {
	if (m_are_guards_enabled) return;

	auto* info = m_root;
	while (info) {
		auto user_ptr = getUserPtrFromAllocationInfo(info);
		void* system_ptr = getSystemFromUser(user_ptr);
		ASSERT(*(u32*)system_ptr == ALLOCATION_GUARD);
		ASSERT(*(u32*)((u8*)user_ptr + info->size) == ALLOCATION_GUARD);

		info = info->next;
	}
}


size_t Allocator::getAllocationOffset() {
	return sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0);
}


size_t Allocator::getNeededMemory(size_t size) {
	return size + sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) << 1 : 0);
}


size_t Allocator::getNeededMemory(size_t size, size_t align) {
	return size + sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) << 1 : 0) + align;
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromSystem(void* system_ptr) {
	return (AllocationInfo*)(m_are_guards_enabled ? (u8*)system_ptr + sizeof(ALLOCATION_GUARD) : system_ptr);
}


void* Allocator::getUserPtrFromAllocationInfo(AllocationInfo* info) {
	return ((u8*)info + sizeof(AllocationInfo));
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromUser(void* user_ptr) {
	return (AllocationInfo*)((u8*)user_ptr - sizeof(AllocationInfo));
}


u8* Allocator::getUserFromSystem(void* system_ptr, size_t align) {
	size_t diff = (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) + sizeof(AllocationInfo);

	if (align) diff += (align - diff % align) % align;
	return (u8*)system_ptr + diff;
}


u8* Allocator::getSystemFromUser(void* user_ptr) {
	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	size_t diff = (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) + sizeof(AllocationInfo);
	if (info->align) diff += (info->align - diff % info->align) % info->align;
	return (u8*)user_ptr - diff;
}


void* Allocator::allocate_aligned(size_t size, size_t align) {
#ifndef LUMIX_DEBUG
	return m_source.allocate_aligned(size, align);
#else
	void* system_ptr;
	AllocationInfo* info;
	u8* user_ptr;

	size_t system_size = getNeededMemory(size, align);
	{
		MutexGuard lock(m_mutex);
		system_ptr = m_source.allocate(system_size);
		user_ptr = getUserFromSystem(system_ptr, align);
		info = new (NewPlaceholder(), getAllocationInfoFromUser(user_ptr)) AllocationInfo();

		info->previous = m_root->previous;
		m_root->previous->next = info;

		info->next = m_root;
		m_root->previous = info;

		m_root = info;

		m_total_size += size;
	} // because of the MutexGuard

	info->align = u16(align);
	info->stack_leaf = m_stack_tree.record();
	info->size = size;
	if (m_is_fill_enabled) {
		memset(user_ptr, UNINITIALIZED_MEMORY_PATTERN, size);
	}

	if (m_are_guards_enabled) {
		*(u32*)system_ptr = ALLOCATION_GUARD;
		*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) = ALLOCATION_GUARD;
	}

	return user_ptr;
#endif
}


void Allocator::deallocate_aligned(void* user_ptr) {
#ifndef LUMIX_DEBUG
	m_source.deallocate_aligned(user_ptr);
#else
	if (user_ptr) {
		AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
		void* system_ptr = getSystemFromUser(user_ptr);
		if (m_is_fill_enabled) {
			memset(user_ptr, FREED_MEMORY_PATTERN, info->size);
		}

		if (m_are_guards_enabled) {
			ASSERT(*(u32*)system_ptr == ALLOCATION_GUARD);
			size_t system_size = getNeededMemory(info->size, info->align);
			ASSERT(*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) == ALLOCATION_GUARD);
		}

		{
			MutexGuard lock(m_mutex);
			if (info == m_root) {
				m_root = info->next;
			}
			info->previous->next = info->next;
			info->next->previous = info->previous;

			m_total_size -= info->size;
		} // because of the MutexGuard

		info->~AllocationInfo();

		m_source.deallocate_aligned((void*)system_ptr);
	}
#endif
}


void* Allocator::reallocate_aligned(void* user_ptr, size_t new_size, size_t old_size, size_t align) {
#ifndef LUMIX_DEBUG
	return m_source.reallocate_aligned(user_ptr, new_size, old_size, align);
#else
	if (user_ptr == nullptr) return allocate_aligned(new_size, align);
	if (new_size == 0) return nullptr;

	void* new_data = allocate_aligned(new_size, align);
	if (!new_data) return nullptr;

	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	memcpy(new_data, user_ptr, info->size < new_size ? info->size : new_size);

	deallocate_aligned(user_ptr);

	return new_data;
#endif
}


} // namespace debug


void enableCrashReporting(bool enable) {
	g_is_crash_reporting_enabled = false;
}


void installUnhandledExceptionHandler() {}


void clearHardwareBreakpoint(u32 breakpoint_idx) { ASSERT(false); /* not implemented */ }
void setHardwareBreakpoint(u32 breakpoint_idx, const void* mem, u32 size) { ASSERT(false); /* not implemented */ }

} // namespace Lumix
