#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/hash_map.h"

namespace
{
	void UT_insert(const char* params)
	{
		Lumix::HashMap<int32_t, int32_t> hash_table;

		LUMIX_EXPECT_TRUE(hash_table.empty());

		size_t values[10] = {
			1, 2, 3, 4, 5, 6, 7, 8, 9, 10
		};

		for (size_t val : values)
		{
			hash_table.insert(val, val);
		}

		for (size_t val : values)
		{
			LUMIX_EXPECT_EQ(hash_table[val], val);
		}
	};
}

REGISTER_TEST("unit_tests/core/hash_map/insert", UT_insert, "")