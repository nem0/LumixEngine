#include "core/lux.h"
#include "unit_tests/unit_test_manager.h"
#include "unit_tests/unit_test.h"

#include "core/hash_map.h"

namespace
{
	void UT_insert(const char* params)
	{
		Lux::HashMap<int32_t, int32_t> hash_table;

		LUX_EXPECT_TRUE(hash_table.empty());
	};
}

REGISTER_TEST("unit_tests/core/hash_map/insert", UT_insert, "")