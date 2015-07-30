#pragma once 


#include <new>
#include "core/lumix.h"


namespace Lumix
{


	class LUMIX_ENGINE_API IAllocator
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


} // namespace Lumix