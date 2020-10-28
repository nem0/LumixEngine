#pragma once


#include "engine/allocator.h"
#include "engine/crt.h"


namespace Lumix
{
	template <typename Key, typename Value>
	struct AssociativeArray
	{
		public:
			explicit AssociativeArray(IAllocator& allocator)
				: m_allocator(allocator)
				, m_size(0)
				, m_capacity(0)
				, m_keys(nullptr)
				, m_values(nullptr)
			{}


			~AssociativeArray()
			{
				callDestructors(m_keys, m_size);
				callDestructors(m_values, m_size);
				m_allocator.deallocate(m_keys);
			}


			Value& insert(const Key& key)
			{
				if (m_capacity == m_size) reserve(m_capacity < 4 ? 4 : m_capacity * 2);

				int i = index(key);
				ASSERT(i >= 0 && ((i < m_size && m_keys[i] != key) || i == m_size));
				memmove(m_keys + i + 1, m_keys + i, sizeof(Key) * (m_size - i));
				memmove(m_values + i + 1, m_values + i, sizeof(Value) * (m_size - i));
				new (NewPlaceholder(), &m_values[i]) Value();
				new (NewPlaceholder(), &m_keys[i]) Key(key);
				++m_size;
				return m_values[i];
			}


			template <typename... Params> Value& emplace(const Key& key, Params&&... params)
			{
				if (m_capacity == m_size) reserve(m_capacity < 4 ? 4 : m_capacity * 2);

				int i = index(key);
				ASSERT(i >= 0 && ((i < m_size && m_keys[i] != key) || i == m_size));

				memmove(m_keys + i + 1, m_keys + i, sizeof(Key) * (m_size - i));
				memmove(m_values + i + 1, m_values + i, sizeof(Value) * (m_size - i));
				new (NewPlaceholder(), &m_values[i]) Value(static_cast<Params&&>(params)...);
				new (NewPlaceholder(), &m_keys[i]) Key(key);
				++m_size;

				return m_values[i];
			}


			int insert(const Key& key, Value&& value)
			{
				if (m_capacity == m_size) reserve(m_capacity < 4 ? 4 : m_capacity * 2);

				int i = index(key);
				if (i >= 0 && ((i < m_size && m_keys[i] != key) || i == m_size))
				{
					memmove(m_keys + i + 1, m_keys + i, sizeof(Key) * (m_size - i));
					memmove(m_values + i + 1, m_values + i, sizeof(Value) * (m_size - i));
					new (NewPlaceholder(), &m_values[i]) Value(static_cast<Value&&>(value));
					new (NewPlaceholder(), &m_keys[i]) Key(key);
					++m_size;

					return i;
				}
				return -1;
			}


			int insert(const Key& key, const Value& value)
			{
				if (m_capacity == m_size) reserve(m_capacity < 4 ? 4 : m_capacity * 2);

				int i = index(key);
				if (i >= 0 && ((i < m_size && m_keys[i] != key) || i == m_size))
				{
					memmove(m_keys + i + 1, m_keys + i, sizeof(Key) * (m_size - i));
					memmove(m_values + i + 1, m_values + i, sizeof(Value) * (m_size - i));
					new (NewPlaceholder(), &m_values[i]) Value(value);
					new (NewPlaceholder(), &m_keys[i]) Key(key);
					++m_size;

					return i;
				}
				return -1;
			}


			int find(const Key& key) const
			{
				int l = 0;
				int h = m_size - 1;
				while (l < h)
				{
					int mid = (l + h) >> 1;
					if (m_keys[mid] < key)
					{
						l = mid + 1;
					}
					else
					{
						h = mid;
					}
				}
				if (l == h && m_keys[l] == key)
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
					return m_values[index];
				}
				else
				{
					ASSERT(false);
					return m_values[0];
				}
			}


			Value& operator [](const Key& key)
			{
				int index = find(key);
				if (index >= 0)
				{
					return m_values[index];
				}
				else
				{
					ASSERT(false);
					return m_values[0];
				}
			}


			int size() const
			{
				return m_size;
			}


			Value& get(const Key& key)
			{
				int index = find(key);
				ASSERT(index >= 0);
				return m_values[index];
			}

			const Value& get(const Key& key) const
			{
				int index = find(key);
				ASSERT(index >= 0);
				return m_values[index];
			}

			Value* begin() { return m_values; }
			Value* end() { return m_values + m_size; }
			const Value* begin() const { return m_values; }
			const Value* end() const { return m_values + m_size; }


			Value& at(int index)
			{
				return m_values[index];
			}


			const Value& at(int index) const
			{
				return m_values[index];
			}


			void clear()
			{
				callDestructors(m_keys, m_size);
				callDestructors(m_values, m_size);
				m_size = 0;
			}


			void reserve(int new_capacity)
			{
				if (m_capacity >= new_capacity) return;
				
				u8* new_data = (u8*)m_allocator.allocate(new_capacity * (sizeof(Key) + sizeof(Value)));
				
				memcpy(new_data, m_keys, sizeof(Key) * m_size);
				memcpy(new_data + sizeof(Key) * new_capacity, m_values, sizeof(Value) * m_size);

				m_allocator.deallocate(m_keys);
				m_keys = (Key*)new_data;
				m_values = (Value*)(new_data + sizeof(Key) * new_capacity);

				m_capacity = new_capacity;
			}


			const Key& getKey(int index) const
			{
				return m_keys[index];
			}


			void eraseAt(int index)
			{
				if (index >= 0 && index < m_size)
				{
					m_values[index].~Value();
					m_keys[index].~Key();
					if (index < m_size - 1)
					{
						memmove(m_keys + index, m_keys + index + 1, sizeof(Key) * (m_size - index - 1));
						memmove(m_values + index, m_values + index + 1, sizeof(Value) * (m_size - index - 1));
					}
					--m_size;
				}
			}


			void erase(const Key& key)
			{
				int i = find(key);
				if (i >= 0)
				{
					eraseAt(i);
				}
			}
			Span<Value> values() const {
				Span<Value> res;
				res.m_begin = m_values;
				res.m_end = m_values + m_size;
				return res;
			}

			Span<Key> keys() const { 
				Span<Key> res;
				res.m_begin = m_keys;
				res.m_end = m_keys + m_size;
				return res;
			}

		private:
			template <typename T> void callDestructors(T* ptr, int count)
			{
				for (int i = 0; i < count; ++i)
				{
					ptr[i].~T();
				}
			}


			int index(const Key& key) const
			{
				int l = 0;
				int h = m_size - 1;
				while (l < h)
				{
					int mid = (l + h) >> 1;
					if (m_keys[mid] < key)
					{
						l = mid + 1;
					}
					else
					{
						h = mid;
					}
				}
				if (l + 1 == m_size && m_keys[l] < key)
				{
					return l + 1;
				}
				return l;
			}

		private:
			IAllocator& m_allocator;
			Key* m_keys;
			Value* m_values;
			int m_size;
			int m_capacity;
	};


} // namespace Lumix
