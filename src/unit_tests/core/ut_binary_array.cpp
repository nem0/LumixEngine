#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/binary_array.h"
#include <cstdio>


void UT_binary_array(const char* params)
{
	Lumix::DefaultAllocator allocator;

	Lumix::BinaryArray array(allocator);
	LUMIX_EXPECT_EQ(array.size(), 0);
	for (int i = 0; i < 100; ++i)
	{
		array.push(true);
	}
	for (int i = 0; i < 100; ++i)
	{
		LUMIX_EXPECT_TRUE(array[i]);
	}
	LUMIX_EXPECT_EQ(array.getRaw()[0], 0xffffFFFF);
	for (int i = 0; i < 100; ++i)
	{
		array.push(false);
	}
	for (int i = 100; i < 200; ++i)
	{
		LUMIX_EXPECT_TRUE(array[i]);
	}
	LUMIX_EXPECT_EQ(array.size(), 200);
	for (int i = 0; i < 150; ++i)
	{
		array.pop();
	}
	LUMIX_EXPECT_EQ(array.size(), 50);
	LUMIX_EXPECT_EQ(array.getRawSize(), 2);
	LUMIX_EXPECT_EQ(array.getRaw()[0], 0xffffFFFF);
	
	array.clear();
	for (int i = 0; i < 100; ++i)
	{
		array.push(i == 2 || i == 50);
	}

	array.erase(50);
	array.erase(2);
	LUMIX_EXPECT_EQ(array.getRaw()[0], 0);
	LUMIX_EXPECT_EQ(array.getRaw()[1], 0);
}

REGISTER_TEST("unit_tests/core/binary_array", UT_binary_array, "")