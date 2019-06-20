#pragma once


#include "engine/allocator.h"


namespace Lumix
{


	class LIFOAllocator final : public IAllocator
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


			void* allocate(size_t size) override
			{
				u8* new_address = (u8*)m_current;
				ASSERT(new_address + size <= (u8*)m_bucket + m_bucket_size);
				m_current = new_address + size + sizeof(size_t);
				*(size_t*)(new_address + size) = size;
				return new_address;
			}


			void deallocate(void* ptr) override
			{
				if (!ptr) return;
				size_t last_size = *(size_t*)((u8*)m_current - sizeof(size_t));
				u8* last_mem = (u8*)m_current - last_size - sizeof(size_t);
				ASSERT(last_mem == ptr);
				m_current = ptr;
			}


			void* reallocate(void* ptr, size_t size) override
			{
				if (!ptr) return allocate(size);

				size_t last_size = *(size_t*)((u8*)m_current - sizeof(size_t));
				u8* last_mem = (u8*)m_current - last_size - sizeof(size_t);
				ASSERT(last_mem == ptr);
				m_current = last_mem + size + sizeof(size_t);
				*(size_t*)(last_mem + size) = size;
				return ptr;
			}


			void* allocate_aligned(size_t size, size_t align) override
			{
				size_t padding = (align - ((uintptr)m_current % align)) % align;
				u8* new_address = (u8*)m_current + padding;
				ASSERT(new_address + size <= (u8*)m_bucket + m_bucket_size);
				m_current = new_address + size + sizeof(size_t);
				*(size_t*)(new_address + size) = size + padding;
				ASSERT((uintptr)new_address % align == 0);
				return new_address;
			}


			void deallocate_aligned(void* ptr) override
			{
				if (!ptr) return;
				m_current = ptr;
			}


			void* reallocate_aligned(void* ptr, size_t size, size_t align) override
			{
				if (!ptr) return allocate_aligned(size, align);

				size_t last_size_with_padding = *(size_t*)((u8*)m_current - sizeof(size_t));
				u8* last_mem = (u8*)m_current - last_size_with_padding - sizeof(size_t);

				size_t padding = (align - ((uintptr)last_mem % align)) % align;
				u8* new_address = (u8*)last_mem + padding;
				ASSERT(new_address + size <= (u8*)m_bucket + m_bucket_size);
				m_current = new_address + size + sizeof(size_t);
				*(size_t*)(new_address + size) = size + padding;
				ASSERT((uintptr)new_address % align == 0);
				return new_address;
			}


		private:
			IAllocator& m_source;
			size_t m_bucket_size;
			void* m_bucket;
			void* m_current;
	};


} // namespace Lumix
