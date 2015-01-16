#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/associative_array.h"


void UT_associative_array(const char* params)
{
	Lumix::DefaultAllocator allocator;

	Lumix::AssociativeArray<int, int> array1(allocator);
	LUMIX_EXPECT_EQ(array1.size(), 0);
	array1.reserve(128);
	LUMIX_EXPECT_EQ(array1.size(), 0);
	int x;
	LUMIX_EXPECT_FALSE(array1.find(0, x));

	for (int i = 0; i < 10; ++i)
	{
		array1.insert(i, i * 5);
	}
	LUMIX_EXPECT_EQ(array1.size(), 10);
	array1.insert(2, 10);
	LUMIX_EXPECT_EQ(array1.size(), 10);
	LUMIX_EXPECT_EQ(array1.get(1), 5);
	LUMIX_EXPECT_EQ(array1.get(3), 15);
	LUMIX_EXPECT_EQ(array1.get(7), 35);
	LUMIX_EXPECT_FALSE(array1.find(11, x));
	array1.erase(5);
	LUMIX_EXPECT_FALSE(array1.find(5, x));
	LUMIX_EXPECT_EQ(array1.size(), 9);
	for (int i = 0; i < 9; ++i)
	{
		LUMIX_EXPECT_EQ(array1.get(array1.getKey(i)), array1.at(i));
		LUMIX_EXPECT_EQ(array1[array1.getKey(i)], array1.at(i));
	}
	for (int i = 0; i < 9; ++i)
	{
		array1[array1.getKey(i)] = array1.getKey(i) * 5;
		LUMIX_EXPECT_EQ(array1[array1.getKey(i)], array1.getKey(i) * 5);
	}

	array1.eraseAt(0);
	LUMIX_EXPECT_EQ(array1.size(), 8);
	array1.clear();
	LUMIX_EXPECT_EQ(array1.size(), 0);
}

REGISTER_TEST("unit_tests/core/associative_array", UT_associative_array, "")