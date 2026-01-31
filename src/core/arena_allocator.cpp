#include "core/arena_allocator.h"
#include "core/atomic.h"
#include "core/crt.h"
#include "core/default_allocator.h"
#include "core/math.h"
#include "core/os.h"
#include "core/tag_allocator.h"
#if !defined __linux__ && defined __clang__
	#include <intrin.h>
#endif
#if !defined _WIN32 || defined __clang__
	#include <malloc.h>
	#include <string.h>
#endif


namespace black
{

ArenaAllocator::ArenaAllocator(u32 reserved, IAllocator& parent, const char* tag)
	: m_parent(parent)
	#ifdef BLACK_DEBUG	
		, m_tag_allocator(parent, tag)
	#endif
{
	m_reserved = reserved;
	m_mem = (u8*)os::memReserve(reserved);
	#ifdef BLACK_DEBUG	
		m_allocation_info.flags = debug::AllocationInfo::IS_ARENA;
		m_allocation_info.tag = &m_tag_allocator;
	#endif
}

ArenaAllocator::~ArenaAllocator() {
	ASSERT(m_end == 0);
	os::memRelease(m_mem, m_reserved);
	#ifdef BLACK_DEBUG	
		debug::unregisterAlloc(m_allocation_info);
	#endif
}

void ArenaAllocator::reset() {
	m_end = 0;
}

static u32 roundUp(u32 val, u32 align) {
	ASSERT(isPowOfTwo(align));
	return (val + align - 1) & ~(align - 1);
}

void* ArenaAllocator::allocate(size_t size, size_t align) {
	ASSERT(size < 0xffFFffFF);
	u32 start;
	for (;;) {
		const u32 end = m_end;
		start = roundUp(end, (u32)align);
		if (m_end.compareExchange(u32(start + size), end)) break;
	}

	if (start + size <= m_commited_bytes) return m_mem + start;

	MutexGuard guard(m_mutex);
	if (start + size <= m_commited_bytes) return m_mem + start;

	const u32 commited = roundUp(start + (u32)size, 4096);
	ASSERT(commited < m_reserved);
	os::memCommit(m_mem + m_commited_bytes, commited - m_commited_bytes);
	m_commited_bytes = commited;

	#ifdef BLACK_DEBUG
		if (m_allocation_info.size == 0) debug::registerAlloc(m_allocation_info);
		debug::resizeAlloc(m_allocation_info, m_commited_bytes);
	#endif

	return m_mem + start;
}

void ArenaAllocator::deallocate(void* ptr) { /*everything should be "deallocated" with reset()*/ }
void* ArenaAllocator::reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) { 
	if (!ptr) return allocate(new_size, align);
	// realloc not supported
	ASSERT(false); 
	return nullptr;
}

TagAllocator::TagAllocator(IAllocator& allocator, const char* tag_name)
	: m_tag(tag_name)
{
	m_effective_allocator = m_direct_parent = &allocator;
	while (m_effective_allocator->getParent() && m_effective_allocator->isTagAllocator()) {
		m_effective_allocator = m_effective_allocator->getParent();
	}
}

thread_local TagAllocator* active_allocator = nullptr;

void* TagAllocator::allocate(size_t size, size_t align) {
	active_allocator = this;
	return m_effective_allocator->allocate(size, align);
}

void TagAllocator::deallocate(void* ptr) {
	m_effective_allocator->deallocate(ptr);
}

void* TagAllocator::reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) {
	active_allocator = this;
	return m_effective_allocator->reallocate(ptr, new_size, old_size, align);
}

TagAllocator* TagAllocator::getActiveAllocator() {
	return active_allocator;
}

IAllocator& getGlobalAllocator() {
	static DefaultAllocator alloc;
	return alloc;
}

} // namespace black
