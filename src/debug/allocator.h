#pragma once


#include "core/lumix.h"
#include "core/iallocator.h"
#include "core/MT/spin_mutex.h"


namespace Lumix
{

	
namespace Debug
{


	class LUMIX_CORE_API Allocator : public IAllocator
	{
		public:
			Allocator(IAllocator& source);
			virtual ~Allocator();

			virtual void* allocate(size_t size) override;
			virtual void deallocate(void* ptr) override;

			IAllocator& getSourceAllocator() { return m_source; }

		private:
			IAllocator& m_source;
			class StackTree* m_stack_tree;
			MT::SpinMutex m_mutex;
			class AllocationInfo* m_root;
	};


} // namespace Debug


} // namespace Lumix