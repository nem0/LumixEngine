#pragma once


#include "engine/core/iallocator.h"


namespace Lumix
{


	class LIFOAllocator : public IAllocator
	{
		public:
			LIFOAllocator(IAllocator& source, size_t bucket_size)
				: m_source(source)
				, m_bucket_size(bucket_size)
			{
				m_bucket = source.allocate(bucket_size);
				m_current = m_bucket;
			}


			~LIFOAllocator()
			{
				m_source.deallocate(m_bucket);
			}


			void clear()
			{
				m_current = m_bucket;
			}


			void* allocate(size_t size) override
			{
				uint8* new_address = (uint8*)m_current;
				ASSERT(new_address + size <= (uint8*)m_bucket + m_bucket_size);
				m_current = new_address + size;
				return new_address;
			}


			void deallocate(void*) override
			{
				ASSERT(false);
			}


			void* reallocate(void*, size_t) override
			{
				ASSERT(false);
				return nullptr;
			}


			void* allocate_aligned(size_t size, size_t align) override
			{
				ASSERT(false);
				return nullptr;
			}


			void deallocate_aligned(void* ptr) override
			{
				ASSERT(false);
			}


			void* reallocate_aligned(void* ptr, size_t size, size_t align) override
			{
				ASSERT(false);
				return nullptr;
			}


		private:
			IAllocator& m_source;
			size_t m_bucket_size;
			void* m_bucket;
			void* m_current;
	};


} // namespace Lumix
