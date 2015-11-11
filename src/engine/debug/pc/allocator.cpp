#include "debug/allocator.h"
#include "debug/stack_tree.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>


namespace Lumix
{


namespace Debug
{


	static const uint32 UNINITIALIZED_MEMORY_PATTERN = 0xCD;
	static const uint32 FREED_MEMORY_PATTERN = 0xDD;
	static const uint32 ALLOCATION_GUARD = 0xFDFDFDFD;


	Allocator::Allocator(IAllocator& source)
		: m_source(source)
		, m_root(nullptr)
		, m_mutex(false)
		, m_stack_tree(m_source.newObject<Debug::StackTree>())
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
		return size + sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) << 1 : 0);
	}


	Allocator::AllocationInfo* Allocator::getAllocationInfoFromSystem(void* system_ptr)
	{
		return (AllocationInfo*)(m_are_guards_enabled ? (uint8*)system_ptr + sizeof(ALLOCATION_GUARD) : system_ptr);
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
		return (uint8*)system_ptr + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) + sizeof(AllocationInfo);
	}


	void* Allocator::getSystemFromUser(void* user_ptr)
	{
		return (uint8*)user_ptr - (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) - sizeof(AllocationInfo);
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
			memcpy(new_data, user_ptr, info->m_size < size ? info->m_size : size);

			deallocate(user_ptr);

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
				info = new (getAllocationInfoFromSystem(system_ptr)) AllocationInfo();

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
					if(info == m_root)
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


} // namespace Lumix