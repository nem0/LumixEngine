#include "debug/allocator.h"
#include "debug/stack_tree.h"

#include <Windows.h>
#include <cstdio>


namespace Lumix
{


namespace Debug
{


	static const uint32_t UNINITIALIZED_MEMORY_PATTERN = 0xCDCDCDCD;
	static const uint32_t FREED_MEMORY_PATTERN = 0xDDDDDDDD;


	Allocator::Allocator(IAllocator& source)
		: m_source(source)
		, m_root(NULL)
		, m_mutex(false)
		, m_stack_tree(m_source.newObject<Debug::StackTree>())
		, m_total_size(0)
		, m_is_fill_enabled(true)
	{
		m_sentinels[0].m_next = &m_sentinels[1];
		m_sentinels[0].m_previous = NULL;
		m_sentinels[0].m_stack_leaf = NULL;
		m_sentinels[0].m_size = 0;

		m_sentinels[1].m_next = NULL;
		m_sentinels[1].m_previous = &m_sentinels[0];
		m_sentinels[1].m_stack_leaf = NULL;
		m_sentinels[1].m_size = 0;

		m_root = &m_sentinels[1];
	}


	Allocator::~Allocator()
	{
		AllocationInfo* last_sentinel = &m_sentinels[1];
		if (m_root != last_sentinel)
		{
			OutputDebugString("Memory leaks detected!\n");
			AllocationInfo* info = m_root;
			while (info != last_sentinel)
			{
				char tmp[2048];
				sprintf(tmp, "\nAllocation size : %d, memory %p\n", info->m_size, info + sizeof(info));
				OutputDebugString(tmp);
				m_stack_tree->printCallstack(info->m_stack_leaf);
				info = info->m_next;
			}
			ASSERT(false);
		}
		m_source.deleteObject(m_stack_tree);
	}


	void* Allocator::allocate(size_t size)
	{
		#ifndef _DEBUG
			return m_source.allocate(size);
		#else
			void* ptr;
			{
				MT::SpinLock lock(m_mutex);
				ptr = m_source.allocate(sizeof(AllocationInfo) + size);
				AllocationInfo* info = new (ptr)AllocationInfo();

				info->m_previous = m_root->m_previous;
				m_root->m_previous->m_next = info;

				info->m_next = m_root;
				m_root->m_previous = info;

				info->m_stack_leaf = m_stack_tree->record();
				info->m_size = size;

				m_root = info;

				m_total_size += size;
			} // because of the SpinLock

			if (m_is_fill_enabled)
			{
				memset((uint8_t*)ptr + sizeof(AllocationInfo), UNINITIALIZED_MEMORY_PATTERN, size);
			}

			return (uint8_t*)ptr + sizeof(AllocationInfo);
		#endif
	}

	void Allocator::deallocate(void* ptr)
	{
		#ifndef _DEBUG
			m_source.deallocate(ptr);
		#else
			if(ptr)
			{
				AllocationInfo* info = reinterpret_cast<AllocationInfo*>((uint8_t*)ptr - sizeof(AllocationInfo));
				if (m_is_fill_enabled)
				{
					memset(ptr, FREED_MEMORY_PATTERN, info->m_size);
				}
				MT::SpinLock lock(m_mutex);
				if(info == m_root)
				{
					m_root = info->m_next;
				}
				info->m_previous->m_next = info->m_next;
				info->m_next->m_previous = info->m_previous;
				
				m_total_size -= info->m_size;
				
				info->~AllocationInfo();

				m_source.deallocate((void*)info);
			}
		#endif
	}


} // namespace Debug


} // namespace Lumix