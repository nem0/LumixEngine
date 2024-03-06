#pragma once


#include "core/allocator.h"
#include "core/core.h"


namespace Lumix
{


template <typename Key> 
struct HashFunc
{
	static u32 get(const Key& key);
};

template<>
struct HashFunc<u64>
{
	static u32 get(u64 key) {
		// https://xoshiro.di.unimi.it/splitmix64.c
		u64 x = key;
		x ^= x >> 30;
		x *= 0xbf58476d1ce4e5b9U;
		x ^= x >> 27;
		x *= 0x94d049bb133111ebU;
		x ^= x >> 31;
		return u32((x >> 32) ^ x);
	}
};


template<>
struct HashFunc<i32> {
	static u32 get(i32 key) {
		// https://nullprogram.com/blog/2018/07/31/
		u32 x = key;
		x ^= x >> 16;
		x *= 0x7feb352dU;
		x ^= x >> 15;
		x *= 0x846ca68bU;
		x ^= x >> 16;
		return x;
	}
};

template<>
struct HashFunc<u16> {
	static u32 get(u16 key) {
		return HashFunc<i32>::get(key);
	}
};

template<>
struct HashFunc<u32> {
	static u32 get(const u32& key) {
		u32 x = key;
		x ^= x >> 16;
		x *= 0x7feb352dU;
		x ^= x >> 15;
		x *= 0x846ca68bU;
		x ^= x >> 16;
		return x;
	}
};

template<typename T>
struct HashFunc<T*>
{
	static u32 get(const void* key)
	{
		static_assert(sizeof(key) == sizeof(u64));
		u64 x = (u64)key;
		x ^= x >> 30;
		x *= 0xbf58476d1ce4e5b9U;
		x ^= x >> 27;
		x *= 0x94d049bb133111ebU;
		x ^= x >> 31;
		return u32((x >> 32) ^ x);
	}
};

template <typename T>
struct HashFuncDirect {
	static u32 get(T key) { return key; }
};

template<typename Key, typename Value, typename Hasher = HashFunc<Key>>
struct HashMap
{
private:
	struct Slot {
		alignas(Key) u8 key_mem[sizeof(Key)];
		bool valid;
		//u8 padding[3];
	};

	template <typename HM, typename K, typename V>
	struct IteratorBase {
		HM* hm;
		u32 idx;

		template <typename HM2, typename K2, typename V2>
		bool operator !=(const IteratorBase<HM2, K2, V2>& rhs) const {
			ASSERT(hm == rhs.hm);
			return idx != rhs.idx;
		}

		template <typename HM2, typename K2, typename V2>
		bool operator ==(const IteratorBase<HM2, K2, V2>& rhs) const {
			ASSERT(hm == rhs.hm);
			return idx == rhs.idx;
		}

		void operator++() { 
			const Slot* keys = hm->m_keys;
			for(u32 i = idx + 1, c = hm->m_capacity; i < c; ++i) {
				if(keys[i].valid) {
					idx = i;
					return;
				}
			}
			idx = hm->m_capacity;
		}

		K& key() {
			ASSERT(hm->m_keys[idx].valid);
			return *((Key*)hm->m_keys[idx].key_mem);
		}

		const V& value() const {
			ASSERT(hm->m_keys[idx].valid);
			return hm->m_values[idx];
		}

		V& value() {
			ASSERT(hm->m_keys[idx].valid);
			return hm->m_values[idx];
		}

		V& operator*() {
			ASSERT(hm->m_keys[idx].valid);
			return hm->m_values[idx];
		}

		bool isValid() const { return idx != hm->m_capacity; }
	};

public:
	using Iterator = IteratorBase<HashMap, Key, Value>;
	using ConstIterator = IteratorBase<const HashMap, const Key, const Value>;

	explicit HashMap(IAllocator& allocator) 
		: m_allocator(allocator) 
	{
	}

	HashMap(u32 size, IAllocator& allocator) 
		: m_allocator(allocator) 
	{
		init(size, true);
	}

	HashMap(HashMap&& rhs)
		: m_allocator(rhs.m_allocator)
	{
		m_keys = rhs.m_keys;
		m_values = rhs.m_values;
		m_capacity = rhs.m_capacity;
		m_size = rhs.m_size;
		m_mask = rhs.m_mask;
		
		rhs.m_keys = nullptr;
		rhs.m_values = nullptr;
		rhs.m_capacity = 0;
		rhs.m_size = 0;
		rhs.m_mask = 0;
	}

	~HashMap()
	{
		for(u32 i = 0, c = m_capacity; i < c; ++i) {
			if (m_keys[i].valid) {
				((Key*)m_keys[i].key_mem)->~Key();
				m_values[i].~Value();
				m_keys[i].valid = false;
			}
		}
		m_allocator.deallocate(m_keys);
		m_allocator.deallocate(m_values);
	}

	HashMap&& move() {
		return static_cast<HashMap&&>(*this);
	}

	void operator =(HashMap&& rhs) = delete;

	Iterator begin() {
		for (u32 i = 0, c = m_capacity; i < c; ++i) {
			if (m_keys[i].valid) return { this, i };
		}
		return { this, m_capacity };
	}

	ConstIterator begin() const {
		for (u32 i = 0, c = m_capacity; i < c; ++i) {
			if (m_keys[i].valid) return { this, i };
		}
		return { this, m_capacity };
	}

	Iterator end() { return Iterator { this, m_capacity }; }
	ConstIterator end() const { return ConstIterator { this, m_capacity }; }

	void clear() {
		for (u32 i = 0; i < m_capacity; ++i) {
			if (m_keys[i].valid) {
				((Key*)m_keys[i].key_mem)->~Key();
				m_values[i].~Value();
				m_keys[i].valid = false;
			}
		}
		m_size = 0;
	}

	ConstIterator find(const Key& key) const {
		return { this, findPos(key) };
	}

	Iterator find(const Key& key) {
		return { this, findPos(key) };
	}
	
	const Value* getFromIndex(u32 index) const {
		if (!m_keys[index].valid) return nullptr;
		return &m_values[index];
	}
	
	Value* getFromIndex(u32 index) {
		if (!m_keys[index].valid) return nullptr;
		return &m_values[index];
	}

	Value& operator[](const Key& key) {
		const u32 pos = findPos(key);
		ASSERT(pos < m_capacity);
		return m_values[pos];
	}
	
	const Value& operator[](const Key& key) const {
		const u32 pos = findPos(key);
		ASSERT(pos < m_capacity);
		return m_values[pos];
	}

	Value& insert(const Key& key) {
		auto iter = insert(key, {});
		return iter.value();
	}

	Iterator insert(const Key& key, Value&& value) {
		if (m_size >= m_capacity * 3 / 4) {
			grow((m_capacity << 1) < 8 ? 8 : m_capacity << 1);
		}

		u32 pos = Hasher::get(key) & m_mask;
		while (m_keys[pos].valid) ++pos;
		if(pos == m_capacity) {
			pos = 0;
			while (m_keys[pos].valid) ++pos;
		}

		new (NewPlaceholder(), m_keys[pos].key_mem) Key(key);
		new (NewPlaceholder(), &m_values[pos]) Value(static_cast<Value&&>(value));
		++m_size;
		m_keys[pos].valid = true;

		return { this, pos };
	}

	Iterator insert(const Key& key, const Value& value) {
		if (m_size >= m_capacity * 3 / 4) {
			grow((m_capacity << 1) < 8 ? 8 : m_capacity << 1);
		}

		u32 pos = Hasher::get(key) & m_mask;
		while (m_keys[pos].valid) ++pos;
		if(pos == m_capacity) {
			pos = 0;
			while (m_keys[pos].valid) ++pos;
		}

		new (NewPlaceholder(), m_keys[pos].key_mem) Key(key);
		new (NewPlaceholder(), &m_values[pos]) Value(value);
		++m_size;
		m_keys[pos].valid = true;

		return { this, pos };
	}

	template <typename F>
	void eraseIf(F predicate) {
		Slot* keys = m_keys;
		for (u32 i = 0; i < m_capacity; ++i) {
			if (!keys[i].valid) continue;
			if (predicate(m_values[i])) {
				((Key*)keys[i].key_mem)->~Key();
				m_values[i].~Value();
				keys[i].valid = false;
				--m_size;

				u32 pos = (i + 1) % m_capacity;
				while (keys[pos].valid) {
					rehash(pos);
					pos = (pos + 1) % m_capacity;
				}
				--i;
			}
		}
	}

	void erase(const Iterator& key) {
		ASSERT(key.isValid());

		Slot* keys = m_keys;
		u32 pos = key.idx;
		((Key*)keys[pos].key_mem)->~Key();
		m_values[pos].~Value();
		keys[pos].valid = false;
		--m_size;

		pos = (pos + 1) & m_mask;
		while (keys[pos].valid) {
			rehash(pos);
			pos = (pos + 1) % m_capacity;
		}
	}

	void erase(const Key& key) {
		const u32 pos = findPos(key);
		if (pos < m_capacity && m_keys[pos].valid) erase(Iterator{this, pos});
	}

	bool empty() const { return m_size == 0; }
	u32 size() const { return m_size; }
	u32 capacity() const { return m_capacity; }

	void reserve(u32 new_capacity) {
		if (new_capacity > m_capacity) grow(nextPow2(new_capacity));
	}

private:
	template <typename T>
	void swap(T& a, T& b) {
		T tmp = static_cast<T&&>(a);
		a = static_cast<T&&>(b);
		b = static_cast<T&&>(tmp);
	}

	static u32 nextPow2(u32 v) {
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}

	void grow(u32 new_capacity) {
		HashMap<Key, Value, Hasher> tmp(new_capacity, m_allocator);
		if (m_size > 0) {
			for(auto iter = begin(); iter.isValid(); ++iter) {
				tmp.insert(iter.key(), static_cast<Value&&>(iter.value()));
			}
		}

		swap(m_capacity, tmp.m_capacity);
		swap(m_size, tmp.m_size);
		swap(m_mask, tmp.m_mask);
		swap(m_keys, tmp.m_keys);
		swap(m_values, tmp.m_values);
	}

	u32 findEmptySlot(const Key& key, u32 end_pos) const {
		const u32 mask = m_mask;
		u32 pos = Hasher::get(key) & mask;
		while (m_keys[pos].valid && pos != end_pos) ++pos;
		if (pos == m_capacity) {
			pos = 0;
			while (m_keys[pos].valid && pos != end_pos) ++pos;
		}
		return pos;
	}

	void rehash(u32 pos) {
		Key& key = *((Key*)m_keys[pos].key_mem);
		const u32 rehashed_pos = findEmptySlot(key, pos);
		if (rehashed_pos != pos) {
			new (NewPlaceholder(), m_keys[rehashed_pos].key_mem) Key(static_cast<Key&&>(key));
			new (NewPlaceholder(), &m_values[rehashed_pos]) Value(static_cast<Value&&>(m_values[pos]));
			
			((Key*)m_keys[pos].key_mem)->~Key();
			m_values[pos].~Value();
			m_keys[pos].valid = false;
			m_keys[rehashed_pos].valid = true;
		}
	}

	u32 findPos(const Key& key) const {
		u32 pos = Hasher::get(key) & m_mask;
		const Slot* LUMIX_RESTRICT keys = m_keys;
		if (!keys) {
			ASSERT(m_capacity == 0);
			return 0;
		}
		while (keys[pos].valid) {
			if (*((Key*)keys[pos].key_mem) == key) return pos;
			++pos;
		}
		if (pos != m_capacity) return m_capacity;
		pos = 0;
		while (keys[pos].valid) {
			if (*((Key*)keys[pos].key_mem) == key) return pos;
			++pos;
		}
		return m_capacity;
	}

	void init(u32 capacity, bool all_invalid) {
		const bool is_pow_2 = capacity && !(capacity & (capacity - 1));
		ASSERT(is_pow_2);
		m_size = 0;
		m_mask = capacity - 1;
		m_keys = (Slot*)m_allocator.allocate(sizeof(Slot) * (capacity + 1), alignof(Slot));
		m_values = (Value*)m_allocator.allocate(sizeof(Value) * capacity, alignof(Value));
		m_capacity = capacity;
		if (all_invalid) {
			for(u32 i = 0; i < capacity; ++i) {
				m_keys[i].valid = false;
			}
		}
		m_keys[capacity].valid = false;
	}

	IAllocator& m_allocator;
	Slot* m_keys = nullptr;
	Value* m_values = nullptr;
	u32 m_capacity = 0;
	u32 m_size = 0;
	u32 m_mask = 0;
};


} // namespace Lumix
