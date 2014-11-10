#pragma once 


#include "core/lumix.h"
#include "core/MT/atomic.h"


namespace Lumix
{


	class LUMIX_CORE_API IAllocator
	{
		public:
			virtual ~IAllocator() {}

			virtual void* allocate(size_t size) = 0;
			virtual void deallocate(void* ptr) = 0;

			template <class T, typename... Args>
			T* newObject(Args&&... params)
			{
				auto mem = allocate(sizeof(T));
				return new (mem) T(std::forward<Args>(params)...);
			}


			template <class T>
			void deleteObject(T* ptr)
			{
				if(ptr)
				{
					ptr->~T();
					deallocate(ptr);
				}
			}
	};


	class BaseProxyAllocator : public IAllocator
	{
		public:
			BaseProxyAllocator(IAllocator& source)
				: m_source(source)
			{
				m_allocation_count = 0;
			}

			virtual ~BaseProxyAllocator()
			{
				ASSERT(m_allocation_count == 0);
			}

			virtual void* allocate(size_t size) override
			{
				MT::atomicIncrement(&m_allocation_count);
				return m_source.allocate(size);
			}

			virtual void deallocate(void* ptr) override
			{
				if (ptr)
				{
					MT::atomicDecrement(&m_allocation_count);
					m_source.deallocate(ptr);
				}
			}

			IAllocator& getSourceAllocator() { return m_source; }

		private:
			IAllocator& m_source;
			volatile int32_t m_allocation_count;
	};


} // namespace Lumix