#include "debug/allocator.h"
#include "debug/stack_tree.h"

#include <Windows.h>
#include <cstdio>


namespace Lumix
{


namespace Debug
{

	class AllocationInfo
	{
		public:
			AllocationInfo* m_previous;
			AllocationInfo* m_next;
			size_t m_size;
			StackNode* m_stack_leaf;
	};


	Allocator::Allocator(IAllocator& source)
		: m_source(source)
		, m_root(NULL)
		, m_mutex(false)
		, m_stack_tree(m_source.newObject<Debug::StackTree>())
	{
	}


	Allocator::~Allocator()
	{
		if(m_root)
		{
			OutputDebugString("Memory leaks detected!\n");
			AllocationInfo* info = m_root;
			while(info)
			{
				char tmp[2048];
				sprintf(tmp, "\nAllocation size : %d\n", info->m_size);
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
			return NULL;
		#endif
		MT::SpinLock lock(m_mutex);
		void* ptr = m_source.allocate(sizeof(AllocationInfo) + size);
		AllocationInfo* info = new (ptr) AllocationInfo();
		info->m_previous = NULL;
		info->m_next = m_root;
		info->m_stack_leaf = m_stack_tree->record();
		if(m_root)
		{
			m_root->m_previous = info;
		}
		info->m_size = size;
		m_root = info;

		return (uint8_t*)ptr + sizeof(AllocationInfo);
	}


	void Allocator::deallocate(void* ptr)
	{
		#ifndef _DEBUG
			return;
		#endif
		if(ptr)
		{
			AllocationInfo* info = reinterpret_cast<AllocationInfo*>((uint8_t*)ptr - sizeof(AllocationInfo));
			MT::SpinLock lock(m_mutex);
			if(info == m_root)
			{
				m_root = info->m_next;
			}
			if(info->m_previous)
			{
				info->m_previous->m_next = info->m_next;
			}
			if(info->m_next)
			{
				info->m_next->m_previous = info->m_previous;
			}
			info->~AllocationInfo();

			m_source.deallocate((void*)info);
		}
	}


} // namespace Debug


} // namespace Lumix