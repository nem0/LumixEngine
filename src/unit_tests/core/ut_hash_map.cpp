#include "unit_tests/suite/lumix_unit_tests.h"

#include "engine/core/array.h"
#include "engine/core/hash_map.h"
#include "engine/debug/debug.h"

namespace
{
	void UT_insert(const char* params)
	{
		Lumix::DefaultAllocator main_allocator;
		Lumix::Debug::Allocator allocator(main_allocator);
		Lumix::HashMap<int32, int32> hash_table(allocator);

		LUMIX_EXPECT(hash_table.empty());

		int32 values[10] = {
			1, 2, 3, 4, 5, 6, 7, 8, 9, 10
		};

		for (int32 val : values)
		{
			hash_table.insert(val, val);
		}

		for (int32 val : values)
		{
			LUMIX_EXPECT(hash_table[val] == val);
		}
	};

	void UT_array(const char* params)
	{
		Lumix::DefaultAllocator main_allocator;
		Lumix::Debug::Allocator allocator(main_allocator);
		Lumix::HashMap<int32, Lumix::Array<int> > hash_table(allocator);

		LUMIX_EXPECT(hash_table.empty());
	};

	void UT_clear(const char* params)
	{
		Lumix::DefaultAllocator main_allocator;
		Lumix::Debug::Allocator allocator(main_allocator);
		Lumix::HashMap<int32, int32> hash_table(allocator);

		LUMIX_EXPECT(hash_table.empty());

		const int32 COUNT = 20;

		for (int32 i = 0; i < COUNT; i++)
		{
			hash_table.insert(i, i);
		}

		for (int32 i = 0; i < COUNT; i++)
		{
			LUMIX_EXPECT(hash_table[i] == i);
		}

		LUMIX_EXPECT(!hash_table.empty());

		hash_table.clear();

		LUMIX_EXPECT(hash_table.empty());

		hash_table.rehash(8);

		for (int32 i = 0; i < COUNT; i++)
		{
			hash_table.insert(i, i);
		}
	};

	void UT_constIterator(const char* params)
	{
		typedef Lumix::HashMap<int32, int32> HashTableType;

		Lumix::DefaultAllocator main_allocator;
		Lumix::Debug::Allocator allocator(main_allocator);
		HashTableType hash_table(allocator);

		LUMIX_EXPECT(hash_table.empty());

		int32 values[10] = {
			1, 2, 3, 4, 5, 6, 7, 8, 9, 10
		};

		for (int32 val : values)
		{
			hash_table.insert(val, val);
		}

		for (int32 val : values)
		{
			LUMIX_EXPECT(hash_table[val] == val);
		}

		const HashTableType& const_hash_table = hash_table;
		for (HashTableType::constIterator const_it = const_hash_table.begin(); const_it != const_hash_table.end(); ++const_it)
		{
			LUMIX_EXPECT(const_it.value() == values[const_it.key() - 1]);
		}
	}
}

REGISTER_TEST("unit_tests/core/hash_map/insert", UT_insert, "")
REGISTER_TEST("unit_tests/core/hash_map/array", UT_array, "")
REGISTER_TEST("unit_tests/core/hash_map/clear", UT_clear, "")
REGISTER_TEST("unit_tests/core/hash_map/constIterator", UT_constIterator, "")

