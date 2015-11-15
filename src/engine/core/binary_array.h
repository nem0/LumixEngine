#pragma once


#include "lumix.h"


namespace Lumix
{


	class IAllocator;


	class LUMIX_ENGINE_API BinaryArray
	{
		public:
			typedef uint32 StoreType;
			static const size_t ITEM_SIZE = sizeof(StoreType);

			class Accessor
			{
				public:
					Accessor(BinaryArray& array, int index);


					Accessor& operator =(const Accessor& value)
					{
						return *this = (bool)value;
					}

					Accessor& operator =(bool value)
					{
						if (value)
						{
							m_array.m_data[m_index >> 5] |= m_array.INDEX_BIT[m_index & 31];
						}
						else
						{
							m_array.m_data[m_index >> 5] &= ~m_array.INDEX_BIT[m_index & 31];
						}
						return *this;
					}


					operator bool() const
					{
						return (m_array.m_data[m_index >> 5] & m_array.INDEX_BIT[m_index & 31]) > 0;
					}

				private:
					BinaryArray& m_array;
					int m_index;
			};

		public:
			BinaryArray(IAllocator& allocator);
			~BinaryArray();

			Accessor operator[](int index);
			Accessor back();
			bool back() const;
			bool operator[](int index) const;
			void reserve(int capacity);
			void resize(int capacity);
			void erase(int index);
			void clear();
			void push(bool value);
			void pop();
			int size() const;
			int getRawSize() const;
			StoreType* getRaw();

		private:
			void grow(int capacity);
			BinaryArray(const BinaryArray& rhs);
			void operator =(const BinaryArray& rhs);

		private:
			static StoreType BINARY_MASK[sizeof(StoreType) << 3];
			static StoreType INDEX_BIT[sizeof(StoreType) << 3];
			IAllocator& m_allocator;
			StoreType* m_data;
			int m_size;
			int m_capacity;
	};


} // namespace Lumix