#include "core/debug.h"
#include "core/default_allocator.h"
#include "core/stack_tree.h"
#include "core/string.h"
#include "core/tag_allocator.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>

namespace Lumix::debug {

static constexpr u32 UNINITIALIZED_MEMORY_PATTERN = 0xCD;
static constexpr u32 FREED_MEMORY_PATTERN = 0xDD;
static constexpr u32 ALLOCATION_GUARD = 0xFDFDFDFD;

struct StackNode {
	void* instruction = nullptr;
	StackNode* next = nullptr;
	StackNode* first_child = nullptr;
	StackNode* parent = nullptr;
};

AtomicI32 StackTree::s_instances = 0;

StackTree::StackTree(IAllocator& allocator)
	: m_allocator(256 * 1024 * 1024, allocator, "Stack tree") {
#ifdef LUMIX_DEBUG
	m_allocator.getAllocationInfo().flags = AllocationInfo::IS_MISC;
#endif
}

StackTree::~StackTree() {
	m_allocator.reset();
}

void StackTree::refreshModuleList() {}

int StackTree::getPath(StackNode* node, Span<StackNode*> output) {
	u32 count = 0;
	while (count < output.length() && node) {
		output[count++] = node;
		node = node->parent;
	}
	return count;
}

StackNode* StackTree::getParent(StackNode* node) {
	return node ? node->parent : nullptr;
}

bool StackTree::getFunction(StackNode* node, Span<char> output, int& line) {
	line = -1;
	if (!node) return false;
	char** symbols = backtrace_symbols(&node->instruction, 1);
	if (!symbols) return false;
	copyString(output, symbols[0]);
	std::free(symbols);
	return true;
}

void StackTree::printCallstack(StackNode* node) {
	while (node) {
		char** symbols = backtrace_symbols(&node->instruction, 1);
		if (symbols) {
			std::fprintf(stderr, "\\t%s\\n", symbols[0]);
			std::free(symbols);
		}
		node = node->parent;
	}
}

StackNode* StackTree::insertChildren(StackNode* root, void** instruction, void** stack) {
	StackNode* node = root;
	while (instruction >= stack) {
		StackNode* child = LUMIX_NEW(m_allocator, StackNode)();
		child->parent = node;
		child->instruction = *instruction;
		node->first_child = child;
		node = child;
		--instruction;
	}
	return node;
}

StackNode* StackTree::find(void** stack, u32 count) {
	if (!count) return nullptr;
	m_srw_lock.enterShared();
	StackNode* node = m_root;
	void** instruction = stack + count - 1;
	while (node) {
		if (node->instruction != *instruction) {
			node = node->next;
			continue;
		}
		if (instruction == stack) {
			m_srw_lock.exitShared();
			return node;
		}
		--instruction;
		node = node->first_child;
	}
	m_srw_lock.exitShared();
	return nullptr;
}

StackNode* StackTree::record() {
	thread_local bool recording = false;
	if (recording) return nullptr;
	recording = true;
	struct RecordingGuard {
		bool& value;
		~RecordingGuard() { value = false; }
	} guard{recording};

	void* stack[256];
	const int count = backtrace(stack, lengthOf(stack));
	if (count <= 0) return nullptr;

	m_srw_lock.enterExclusive();
	void** instruction = stack + count - 1;
	if (!m_root) {
		m_root = LUMIX_NEW(m_allocator, StackNode)();
		m_root->instruction = *instruction;
		--instruction;
		StackNode* result = insertChildren(m_root, instruction, stack);
		m_srw_lock.exitExclusive();
		return result;
	}

	StackNode* node = m_root;
	while (instruction >= stack) {
		while (node->instruction != *instruction && node->next) node = node->next;
		if (node->instruction != *instruction) {
			node->next = LUMIX_NEW(m_allocator, StackNode)();
			node->next->parent = node->parent;
			node->next->instruction = *instruction;
			StackNode* result = insertChildren(node->next, instruction - 1, stack);
			m_srw_lock.exitExclusive();
			return result;
		}
		if (node->first_child) {
			--instruction;
			node = node->first_child;
		} else if (instruction != stack) {
			StackNode* result = insertChildren(node, instruction - 1, stack);
			m_srw_lock.exitExclusive();
			return result;
		} else {
			m_srw_lock.exitExclusive();
			return node;
		}
	}
	m_srw_lock.exitExclusive();
	return node;
}

struct AllocationDebugSystem {
	AllocationInfo* root = nullptr;
	Mutex mutex;
	AtomicI64 total_size = 0;
} static s_allocations;

static Local<StackTree> s_stack_tree;

void debugOutput(const char* message) {
	std::fputs(message, stdout);
}
void debugBreak() {
	std::abort();
}
void enableFloatingPointTraps(bool) {}

void init(IAllocator& allocator) {
	s_stack_tree.create(allocator);
}

void shutdown() {
	s_stack_tree.destroy();
}

static AllocationInfo* getInfo(void* user_ptr) {
	return (AllocationInfo*)((u8*)user_ptr - sizeof(AllocationInfo));
}

static size_t getOffset() {
	return sizeof(ALLOCATION_GUARD) + sizeof(AllocationInfo);
}

static size_t getNeededMemory(size_t size, size_t align) {
	return size + sizeof(AllocationInfo) + sizeof(ALLOCATION_GUARD) * 2 + align;
}

static u8* getUser(void* system_ptr, size_t align) {
	size_t offset = getOffset();
	if (align) offset += (align - offset % align) % align;
	return (u8*)system_ptr + offset;
}

static u8* getSystem(void* user_ptr) {
	AllocationInfo* info = getInfo(user_ptr);
	size_t offset = sizeof(ALLOCATION_GUARD) + sizeof(AllocationInfo);
	if (info->align) offset += (info->align - offset % info->align) % info->align;
	return (u8*)user_ptr - offset;
}

void registerAlloc(AllocationInfo& info) {
	info.stack_leaf = s_stack_tree.get() ? s_stack_tree->record() : nullptr;
	MutexGuard guard(s_allocations.mutex);
	info.previous = nullptr;
	info.next = s_allocations.root;
	if (s_allocations.root) s_allocations.root->previous = &info;
	s_allocations.root = &info;
	if (!info.is(AllocationInfo::IS_VRAM)) s_allocations.total_size.add(info.size);
}

void unregisterAlloc(const AllocationInfo& info) {
	MutexGuard guard(s_allocations.mutex);
	if (&info == s_allocations.root) s_allocations.root = info.next;
	if (info.previous) info.previous->next = info.next;
	if (info.next) info.next->previous = info.previous;
	if (!info.is(AllocationInfo::IS_VRAM)) s_allocations.total_size.subtract(info.size);
}

void resizeAlloc(AllocationInfo& info, u64 new_size) {
	MutexGuard guard(s_allocations.mutex);
	if (!info.is(AllocationInfo::IS_VRAM)) {
		s_allocations.total_size.subtract(info.size);
		s_allocations.total_size.add(new_size);
	}
	info.size = new_size;
}

u64 getRegisteredAllocsSize() {
	return s_allocations.total_size;
}

const AllocationInfo* lockAllocationInfos() {
	s_allocations.mutex.enter();
	return s_allocations.root;
}

void unlockAllocationInfos() {
	s_allocations.mutex.exit();
}

void checkGuards() {
	MutexGuard guard(s_allocations.mutex);
	for (AllocationInfo* info = s_allocations.root; info; info = info->next) {
		if (info->is(AllocationInfo::IS_VRAM) || info->is(AllocationInfo::IS_PAGED) || info->is(AllocationInfo::IS_ARENA) || info->is(AllocationInfo::IS_MISC)) continue;
		void* system = getSystem((u8*)info + sizeof(AllocationInfo));
		ASSERT(*(u32*)system == ALLOCATION_GUARD);
		ASSERT(*(u32*)((u8*)info + sizeof(AllocationInfo) + info->size) == ALLOCATION_GUARD);
	}
}

void checkLeaks() {
#ifdef LUMIX_DEBUG
	const AllocationInfo* info = lockAllocationInfos();
	bool leaked = false;
	for (; info; info = info->next) {
		if (!info->is(AllocationInfo::IS_MISC)) {
			leaked = true;
			std::fprintf(stderr, "Memory leak: %zu bytes\n", info->size);
		}
	}
	unlockAllocationInfos();
	if (leaked) debugOutput("Memory leaks detected!\n");
#endif
}

Allocator::Allocator(IAllocator& source)
	: m_source(source)
	, m_is_fill_enabled(true) {}

void* Allocator::allocate(size_t size, size_t align) {
#ifndef LUMIX_DEBUG
	return m_source.allocate(size, align);
#else
	const size_t system_size = getNeededMemory(size, align);
	void* system = m_source.allocate(system_size, align);
	if (!system) return nullptr;
	u8* user = getUser(system, align);
	auto* info = new (NewPlaceholder(), getInfo(user)) AllocationInfo();
	info->tag = TagAllocator::getActiveAllocator();
	info->size = size;
	info->align = (u16)align;
	registerAlloc(*info);
	m_total_size.add(size);
	if (m_is_fill_enabled) std::memset(user, UNINITIALIZED_MEMORY_PATTERN, size);
	*(u32*)system = ALLOCATION_GUARD;
	*(u32*)(user + size) = ALLOCATION_GUARD;
	return user;
#endif
}

void Allocator::deallocate(void* user) {
#ifndef LUMIX_DEBUG
	m_source.deallocate(user);
#else
	if (!user) return;
	AllocationInfo* info = getInfo(user);
	void* system = getSystem(user);
	ASSERT(*(u32*)system == ALLOCATION_GUARD);
	ASSERT(*(u32*)((u8*)user + info->size) == ALLOCATION_GUARD);
	if (m_is_fill_enabled) std::memset(user, FREED_MEMORY_PATTERN, info->size);
	m_total_size.subtract(info->size);
	unregisterAlloc(*info);
	info->~AllocationInfo();
	m_source.deallocate(system);
#endif
}

void* Allocator::reallocate(void* user, size_t new_size, size_t old_size, size_t align) {
	if (!user) return allocate(new_size, align);
	if (!new_size) {
		deallocate(user);
		return nullptr;
	}
	void* result = allocate(new_size, align);
	if (!result) return nullptr;
	AllocationInfo* info = getInfo(user);
	std::memcpy(result, user, info->size < new_size ? info->size : new_size);
	deallocate(user);
	return result;
}

} // namespace Lumix::debug

namespace Lumix {
void configureCrashReport(CrashReportFlags) {}
void installUnhandledExceptionHandler() {}
void clearHardwareBreakpoint(u32) {
	ASSERT(false);
}
void setHardwareBreakpoint(u32, const void*, u32) {
	ASSERT(false);
}
} // namespace Lumix
