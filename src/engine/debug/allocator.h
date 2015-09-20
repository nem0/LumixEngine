#pragma once


#include "lumix.h"
#include "core/iallocator.h"
#include "core/MT/spin_mutex.h"


namespace Lumix
{

	
namespace Debug
{


	class StackNode;
	class StackTree;


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
			Allocator(IAllocator& source);
			virtual ~Allocator();

			virtual void* allocate(size_t size) override;
			virtual void deallocate(void* ptr) override;
			virtual void* reallocate(void* ptr, size_t size) override;
			size_t getTotalSize() const { return m_total_size; }

			IAllocator& getSourceAllocator() { return m_source; }
			AllocationInfo* getFirstAllocationInfo() const { return m_root; }

		private:
			inline size_t getAllocationOffset();
			inline AllocationInfo* getAllocationInfoFromSystem(void* system_ptr);
			inline AllocationInfo* getAllocationInfoFromUser(void* user_ptr);
			inline void* getUserFromSystem(void* system_ptr);
			inline void* getSystemFromUser(void* user_ptr);
			inline size_t getNeededMemory(size_t size);

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


} // namespace Lumix