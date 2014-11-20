#pragma once


#include "core/array.h"


namespace Lumix
{

	template <typename T> 
	class SortedArray
	{
		public:
			SortedArray(IAllocator& allocator)
				: m_data(allocator)
			{}


			int size() const { return m_data.size(); }


			T& operator[](int index)
			{
				return m_data[index];
			}


			const T& operator[](int index) const
			{
				return m_data[index];
			}


			int insert(const T& value)
			{
				if (m_data.empty())
				{
					m_data.push(value);
					return 0;
				}
				else
				{
					int i = index(value);
					if (i >= 0 && i < m_data.size() && m_data[i] != value)
					{
						m_data.insert(i, value);
						return i;
					}
				}
				return -1;
			}


			bool contains(const T& value)
			{
				int i = index(value);
				return i < m_data.size() && m_data[i] == value;
			}


		private:
			int index(const T& value) const
			{
				int l = 0;
				int h = m_data.size() - 1;
				while (l < h)
				{
					int mid = (l + h) >> 1;
					if (m_data[mid] < value)
					{
						l = mid + 1;
					}
					else 
					{
						h = mid;
					}
				}
				if (l + 1 == m_data.size() && m_data[l] < value)
				{
					return l + 1;
				}
				return l;
			}

		private:
			Array<T> m_data;
	};

		
	template <typename Key, typename Value>
	class AssociativeArray
	{
		public:
			AssociativeArray(IAllocator& allocator)
				: m_data(allocator)
			{}


			void insert(const Key& key, const Value& value)
			{
				m_data.insert(Pair(key, value));
			}


			int find(const Key& key) const
			{
				int l = 0;
				int h = m_data.size() - 1;
				while (l < h)
				{
					int mid = (l + h) >> 1;
					if (m_data[mid].m_key < key)
					{
						l = mid + 1;
					}
					else
					{
						h = mid;
					}
				}
				if (l == h && m_data[l].m_key == key)
				{
					return l;
				}
				return -1;
			}


			const Value& operator [](const Key& key) const
			{
				int index = find(key);
				if (index >= 0)
				{
					return m_data[index].m_value;
				}
				else
				{
					return m_data[m_data.insert(Pair(key, Value()))].m_value;
				}
			}


			Value& operator [](const Key& key)
			{
				int index = find(key);
				if (index >= 0)
				{
					return m_data[index].m_value;
				}
				else
				{
					return m_data[m_data.insert(Pair(key, Value()))].m_value;
				}
			}

		private:
			struct Pair
			{
				Pair() {}
				Pair(const Key& key, const Value& value) : m_key(key), m_value(value) {}

				bool operator <(const Pair& rhs) const { return m_key < rhs.m_key; }
				bool operator >(const Pair& rhs) const { return m_key > rhs.m_key; }
				bool operator ==(const Pair& rhs) const { return m_key == rhs.m_key; }
				bool operator !=(const Pair& rhs) const { return m_key != rhs.m_key; }

				Key m_key;
				Value m_value;
			};

		private:
			SortedArray<Pair> m_data;
	};


} // namespace Lumix