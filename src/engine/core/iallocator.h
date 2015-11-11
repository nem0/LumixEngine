#pragma once 


#include "lumix.h"
#include <new>


namespace Lumix
{


	class LUMIX_ENGINE_API IAllocator
	{
		public:
			virtual ~IAllocator() {}

			virtual void* allocate(size_t size) = 0;
			virtual void deallocate(void* ptr) = 0;
			virtual void* reallocate(void* ptr, size_t size) = 0;

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


} // namespace Lumix