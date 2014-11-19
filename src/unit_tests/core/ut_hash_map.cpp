#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/array.h"
#include "core/hash_map.h"

namespace
{
	void UT_insert(const char* params)
	{
		Lumix::DefaultAllocator allocator;
		Lumix::HashMap<int32_t, int32_t> hash_table(allocator);

		LUMIX_EXPECT_TRUE(hash_table.empty());
	};

	void UT_array(const char* params)
	{
		Lumix::DefaultAllocator allocator;
		Lumix::HashMap<int32_t, Lumix::Array<int> > hash_table(allocator);

		LUMIX_EXPECT_TRUE(hash_table.empty());
	};

	void UT_clear(const char* params)
	{
		Lumix::DefaultAllocator allocator;
		Lumix::HashMap<int32_t, int32_t> hash_table(allocator);

		LUMIX_EXPECT_TRUE(hash_table.empty());

		const size_t COUNT = 20;

		for (size_t i = 0; i < COUNT; i++)
		{
			hash_table.insert(i, i);
		}

		for (size_t i = 0; i < COUNT; i++)
		{
			LUMIX_EXPECT_EQ(hash_table[i], i);
		}

		LUMIX_EXPECT_FALSE(hash_table.empty());

		hash_table.clear();

		LUMIX_EXPECT_TRUE(hash_table.empty());

		hash_table.rehash(8);

		for (size_t i = 0; i < COUNT; i++)
		{
			hash_table.insert(i, i);
		}
	};
}

REGISTER_TEST("unit_tests/core/hash_map/insert", UT_insert, "")
REGISTER_TEST("unit_tests/core/hash_map/array", UT_array, "")
REGISTER_TEST("unit_tests/core/hash_map/clear", UT_clear, "")
