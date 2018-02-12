#pragma once


#include "engine/lumix.h"


namespace Lumix
{


	struct IAllocator;


	class LUMIX_ENGINE_API BinaryArray
	{
		public:
			typedef u32 StoreType;
			static const size_t ITEM_SIZE = sizeof(StoreType);

			class LUMIX_ENGINE_API Accessor
			{
				public:
					Accessor(BinaryArray& array, int index);
					Accessor(const Accessor& rhs) : m_array(rhs.m_array), m_index(rhs.m_index) {}


					Accessor& operator =(const Accessor& value)
					{
						return *this = (bool)value;
					}

					Accessor& operator =(bool value);
					operator bool() const;

				private:
					BinaryArray& m_array;
					int m_index;
			};

		public:
			explicit BinaryArray(IAllocator& allocator);
			BinaryArray(const BinaryArray& rhs) = delete;
			void operator =(const BinaryArray& rhs) = delete;
			~BinaryArray();

			Accessor operator[](int index);
			Accessor back();
			bool back() const;
			bool operator[](int index) const;
			void reserve(int capacity);
			void resize(int capacity);
			void erase(int index);
			void eraseFast(int index);
			void clear();
			void setAllZeros();
			void push(bool value);
			void pop();
			int size() const;
			int getRawSize() const;
			StoreType* getRaw();

		private:
			void grow(int capacity);

		private:
			IAllocator& m_allocator;
			StoreType* m_data;
			int m_size;
			int m_capacity;
	};


} // namespace Lumix