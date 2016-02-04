#pragma once


#include "lumix.h"
#include "core/iallocator.h"


namespace Lumix
{
	template <class K, class V>
	struct HashNode
	{
		typedef HashNode<K, V> my_node;

		HashNode(const K& key, const V& value)
			: m_key(key)
			, m_value(value)
			, m_next(nullptr)
		{}

		explicit HashNode(const my_node& src)
			: m_key(src.m_key)
			, m_value(src.m_value)
			, m_next(src.m_next)
		{}

		K m_key;
		V m_value;
		my_node* m_next;
	};

	template<class Key> 
	struct HashFunc
	{
		static uint32 get(const Key& key);
	};

	template<>
	struct HashFunc<int32>
	{
		static uint32 get(const int32& key)
		{
			uint32 x = ((key >> 16) ^ key) * 0x45d9f3b;
			x = ((x >> 16) ^ x) * 0x45d9f3b;
			x = ((x >> 16) ^ x);
			return x;
		}
	};

	template<>
	struct HashFunc<uint32>
	{
		static uint32 get(const uint32& key)
		{
			uint32 x = ((key >> 16) ^ key) * 0x45d9f3b;
			x = ((x >> 16) ^ x) * 0x45d9f3b;
			x = ((x >> 16) ^ x);
			return x;
		}
	};

	template<>
	struct HashFunc<void*>
	{
		static size_t get(const void* key)
		{
			size_t x = ((int32(key) >> 16) ^ int32(key)) * 0x45d9f3b;
			x = ((x >> 16) ^ x) * 0x45d9f3b;
			x = ((x >> 16) ^ x);
			return x;
		}
	};

	template<>
	struct HashFunc<char*>
	{
		static uint32 get(const char* key)
		{
			uint32 result = 0x55555555;

			while (*key) 
			{ 
				result ^= *key++;
				result = ((result << 5) | (result >> 27));
			}

			return result;
		}
	};

	template<class K, class T, class Hasher = HashFunc<K>>
	class HashMap
	{
	public:
		typedef T value_type;
		typedef K key_type;
		typedef Hasher hasher_type;
		typedef HashMap<key_type, value_type, hasher_type> my_type;
		typedef HashNode<key_type, value_type> node_type;
		typedef uint32 size_type;

		friend class HashMapIterator;
		friend class ConstHashMapIterator;

		static const size_type s_default_ids_count = 8;

		template <class U, class S, class _Hasher>
		class HashMapIterator
		{
		public:
			typedef U key_type;
			typedef S value_type;
			typedef _Hasher hasher_type;
			typedef HashNode<key_type, value_type> node_type;
			typedef HashMap<key_type, value_type, hasher_type> hm_type;
			typedef HashMapIterator<key_type, value_type, hasher_type> my_type;

			friend class hm_type;

			HashMapIterator()
				: m_hash_map(nullptr)
				, m_current_node(nullptr)
			{
			}

			HashMapIterator(const my_type& src)
				: m_hash_map(src.m_hash_map)
				, m_current_node(src.m_current_node)
			{
			}

			HashMapIterator(node_type* node, hm_type* hm)
				: m_hash_map(hm)
				, m_current_node(node)
			{
			}

			~HashMapIterator()
			{
			}

			bool isValid() const
			{
				return nullptr != m_current_node && m_hash_map->m_sentinel != m_current_node;
			}

			key_type& key()
			{
				return m_current_node->m_key;
			}

			value_type& value()
			{
				return m_current_node->m_value;
			}

			value_type& operator*()
			{
				return value();
			}

			my_type& operator++()
			{
				return preInc();
			}

			my_type operator++(int)
			{
				return postInc();
			}

			bool operator==(const my_type& it) const
			{
				return it.m_current_node == m_current_node;
			}

			bool operator!=(const my_type& it) const
			{
				return it.m_current_node != m_current_node;
			}

		private:
			my_type& preInc()
			{
				m_current_node = m_hash_map->next(m_current_node);
				return *this;
			}

			my_type postInc()
			{
				my_type p = *this;
				m_current_node = m_hash_map->next(m_current_node);
				return p;
			}

			hm_type* m_hash_map;
			node_type* m_current_node;
		};

		template <class U, class S, class _Hasher>
		class ConstHashMapIterator
		{
		public:
			typedef U key_type;
			typedef S value_type;
			typedef _Hasher hasher_type;
			typedef HashNode<key_type, value_type> node_type;
			typedef HashMap<key_type, value_type, hasher_type> hm_type;
			typedef ConstHashMapIterator<key_type, value_type, hasher_type> my_type;

			friend class hm_type;

			ConstHashMapIterator()
				: m_hash_map(nullptr)
				, m_current_node(nullptr)
			{
			}

			ConstHashMapIterator(const my_type& src)
				: m_hash_map(src.m_hash_map)
				, m_current_node(src.m_current_node)
			{
			}

			ConstHashMapIterator(node_type* node, const hm_type* hm)
				: m_hash_map(hm)
				, m_current_node(node)
			{
			}

			~ConstHashMapIterator()
			{
			}

			bool isValid() const
			{
				return nullptr != m_current_node && m_hash_map->m_sentinel != m_current_node;
			}

			const key_type& key() const
			{
				return m_current_node->m_key;
			}

			const value_type& value() const
			{
				return m_current_node->m_value;
			}

			const value_type& operator*() const
			{
				return value();
			}

			my_type& operator++()
			{
				return preInc();
			}

			my_type operator++(int)
			{
				return postInc();
			}

			bool operator==(const my_type& it) const
			{
				return it.m_current_node == m_current_node;
			}

			bool operator!=(const my_type& it) const
			{
				return it.m_current_node != m_current_node;
			}

		private:
			my_type& preInc()
			{
				m_current_node = m_hash_map->next(m_current_node);
				return *this;
			}

			my_type postInc()
			{
				my_type p = *this;
				m_current_node = m_hash_map->next(m_current_node);
				return p;
			}

			const hm_type* m_hash_map;
			node_type* m_current_node;
		};

		typedef HashMapIterator<key_type, value_type, hasher_type> iterator;
		typedef ConstHashMapIterator<key_type, value_type, hasher_type> constIterator;

		HashMap(IAllocator& allocator)
			: m_allocator(allocator)
		{
			initSentinel();
			init();
		}

		HashMap(size_type buckets, IAllocator& allocator)
			: m_allocator(allocator)
		{
			initSentinel();
			init(buckets);
		}

		HashMap(const my_type& src)
			: m_allocator(src.m_allocator)
		{
			initSentinel();
			init(src.m_max_id);
			copyTableUninitialized(src.m_table, src.m_sentinel, src.m_max_id);

			m_mask = src.m_mask;
			m_size = src.m_size;
		}

		~HashMap()
		{
			clear();
			m_allocator.deallocate(m_sentinel);
		}

		size_type size() const { return m_size; }
		bool empty() const { return 0 == m_size; }

		float loadFactor() const { return float(m_size / m_max_id); }
		float maxLoadFactor() const { return 0.75f; }

		my_type& operator=(const my_type& src)
		{
			if(this != &src)
			{
				clear();
				init(src.m_max_id);
				copyTableUninitialized(src.m_table, src.m_sentinel, src.m_max_id);

				m_mask = src.m_mask;
				m_size = src.m_size;
			}

			return *this;
		}

		value_type& operator[](const key_type& key)
		{
			node_type* n = _find(key);
			ASSERT(m_sentinel != n);
			return n->m_value;
		}

		// modifiers
		void insert(const key_type& key, const value_type& val)
		{
			size_type pos = getPosition(key);
			construct(getEmptyNode(pos), key, val);
			m_size++;
			checkSize();
		}

		iterator erase(iterator it)
		{
			ASSERT(it.isValid());

			size_type idx = getPosition(it.m_current_node->m_key);
			node_type* n = &m_table[idx];
			node_type* prev = nullptr;
			node_type* next_it = nullptr;

			while(nullptr != n && m_sentinel != n->m_next)
			{
				if(n == it.m_current_node)
				{
					next_it = next(n);
					deleteNode(n, prev);

					--m_size;
					return iterator(next_it, this);
				}

				prev = n;
				n = n->m_next;
			}

			return iterator(m_sentinel, this);
		}

		size_type erase(const key_type& key)
		{
			size_type count = 0;
			size_type idx = getPosition(key);
			node_type* n = &m_table[idx];
			node_type* prev = nullptr;

			while(nullptr != n && m_sentinel != n->m_next)
			{
				if(key == n->m_key)
				{
					deleteNode(n, prev);

					count++;
					--m_size;
				}
				else
				{
					prev = n;
					n = n->m_next;
				}
			}

			return count;
		}

		void clear()
		{
			for(node_type* n = first(); m_sentinel != n; )
			{
				node_type* dest = n;
				n = next(n);
				destruct(dest);
				if(dest < m_table || dest > &m_table[m_max_id - 1])
					m_allocator.deallocate(dest);
			}

			m_allocator.deallocate(m_table);
			m_table = nullptr;
			m_size = 0;
			m_max_id = 0;
			m_mask = 0;
		}

		void rehash(size_type ids_count)
		{
			if (m_max_id < ids_count)
			{
				grow(ids_count);
			}
		}

		iterator begin() { return iterator(first(), this); }
		iterator end() { return iterator(m_sentinel, this); }

		constIterator begin() const { return constIterator(first(), this); }
		constIterator end() const { return constIterator(m_sentinel, this); }

		iterator find(const key_type& key) { return iterator(_find(key), this); }
		constIterator find(const key_type& key) const { return constIterator(_find(key), this); }

		value_type& at(const key_type& key)
		{
			node_type* n = _find(key);
			return n->m_value;
		}
		
	private:
		void checkSize()
		{
			if(loadFactor() > maxLoadFactor())
			{
				grow(m_max_id);
			}
		}

		size_type getPosition(const key_type& key) const
		{
			size_type pos = Hasher::get(key) & m_mask;
			ASSERT(pos < m_max_id);
			return pos;
		}

		void init(size_type ids_count = s_default_ids_count)
		{
			ASSERT(Math::isPowOfTwo(ids_count));
			m_table = (node_type*)m_allocator.allocate(sizeof(node_type) * ids_count);
			for(node_type* i = m_table; i < &m_table[ids_count]; i++)
				construct(i, m_sentinel);

			m_mask = (ids_count - 1);
			m_max_id = ids_count;
			m_size = 0;
		}

		void grow(size_type ids_count)
		{
			size_type old_ids_count = m_max_id;
			size_type old_size = m_size;
			size_type new_ids_count = ids_count < 512 ? ids_count * 4 : ids_count * 2;
			node_type* old = m_table;

			init(new_ids_count);
			copyTableUninitialized(old, m_sentinel, old_ids_count);
			destructTable(old, m_sentinel, old_ids_count);

			m_size = old_size;
			m_allocator.deallocate(old);
		}

		node_type* construct(node_type* where, const key_type& key, const value_type& val)
		{
			return new(NewPlaceholder(), where) node_type(key, val);
		}

		node_type* construct(node_type* where, node_type* node)
		{
			where->m_next = node;
			return where;
		}

		node_type* construct(node_type* where, const node_type& node)
		{
			return new(NewPlaceholder(), where) node_type(node);
		}

		void destruct(node_type* n)
		{
			(void)n; /// to avoid a bug in VS 2013 - n is unused warning
			n->~node_type();
		}

		void initSentinel()
		{
			m_sentinel = reinterpret_cast<node_type*>(m_allocator.allocate(sizeof(node_type)));
			m_sentinel->m_next = m_sentinel;
		}

		void copyUninitialized(node_type* src, node_type* dst)
		{
			construct(dst, src->m_key, src->m_value);
		}

		node_type* getEmptyNode(size_type pos)
		{
			node_type* node = &m_table[pos];
			while (m_sentinel != node->m_next && nullptr != node->m_next)
			{
				node = node->m_next;
			}

			node->m_next = (nullptr == node->m_next ? construct(reinterpret_cast<node_type*>(m_allocator.allocate(sizeof(node_type))), m_sentinel) : node);
			return node->m_next;
		}

		node_type* first() const
		{
			if(0 == m_size)
				return m_sentinel;

			for(size_type i = 0; i < m_max_id; i++)
				if(m_table[i].m_next != m_sentinel)
					return &m_table[i];

			return m_sentinel;
		}

		node_type* next(node_type* n) const
		{
			if(0 == m_size || m_sentinel == n)
				return m_sentinel;

			node_type* next = n->m_next;
			if((nullptr == next || m_sentinel == next))
			{
				size_type idx = getPosition(n->m_key) + 1;
				for(size_type i = idx; i < m_max_id; i++)
					if(m_table[i].m_next != m_sentinel)
						return &m_table[i];

				return m_sentinel;
			}
			else
			{
				return next;
			}
		}

		node_type* _find(const key_type& key)
		{
			size_type pos = getPosition(key);
			for(node_type* n = &m_table[pos]; nullptr != n && m_sentinel != n->m_next; n = n->m_next)
			{
				if(n->m_key == key)
					return n;
			}

			return m_sentinel;
		}

		void deleteNode(node_type*& n, node_type* prev)
		{
			if(nullptr == prev)
			{
				node_type* next = n->m_next;
				destruct(n);
				construct(n, next ? *next : *m_sentinel);
				if (next)
				{
					destruct(next);
				}
				m_allocator.deallocate(next);
			}
			else
			{
				prev->m_next = n->m_next;
				destruct(n);
				m_allocator.deallocate(n);
				n = prev->m_next;
			}
		}

		void copyTableUninitialized(node_type* src, const node_type* src_sentinel, size_type ids_count)
		{
			for(size_type i = 0; i < ids_count; i++)
			{
				node_type* n = &src[i];
				while(nullptr != n && src_sentinel != n->m_next)
				{
					size_type pos = getPosition(n->m_key);
					node_type* new_node = getEmptyNode(pos);
					copyUninitialized(n, new_node);
					new_node->m_next = nullptr;
					n = n->m_next;
				}
			}
		}

		void destructTable(node_type* src, const node_type* src_sentinel, size_type ids_count)
		{
			for(size_type i = 0; i < ids_count; i++)
			{
				node_type* n = &src[i];
				while(nullptr != n && src_sentinel != n->m_next)
				{
					node_type* destr = n;
					n = n->m_next;
					destruct(destr);
					if(destr < src || destr > &src[ids_count - 1])
						m_allocator.deallocate(destr);
				}
			}
		}

		node_type* m_table;
		node_type* m_sentinel;
		size_type m_size;
		size_type m_mask;
		size_type m_max_id;
		IAllocator& m_allocator;
	};
} // ~namespace Lumix
