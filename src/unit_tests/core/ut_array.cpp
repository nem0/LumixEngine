#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/core/array.h"


void UT_array(const char* params)
{
	Lumix::DefaultAllocator allocator;

	Lumix::Array<int> array1(allocator);
	LUMIX_EXPECT(array1.empty());
	LUMIX_EXPECT(array1.size() == 0);
	array1.reserve(128);
	LUMIX_EXPECT(array1.size() == 0);
	LUMIX_EXPECT(array1.capacity() == 128);
	array1.reserve(256);
	LUMIX_EXPECT(array1.capacity() == 256);
	array1.reserve(64);
	LUMIX_EXPECT(array1.capacity() >= 64);
	LUMIX_EXPECT(array1.size() == 0);

	for (int i = 0; i < 10; ++i)
	{
		array1.push(i * 2);
		LUMIX_EXPECT(array1[i] == i * 2);
		LUMIX_EXPECT(array1.back() == i * 2);
		LUMIX_EXPECT(array1.indexOf(i*2) == i);
	}
	LUMIX_EXPECT(array1.size() == 10);

	for (int i = 0; i < 10; ++i)
	{
		LUMIX_EXPECT(array1[i] == i * 2);
	}

	array1.clear();
	LUMIX_EXPECT(array1.size() == 0);
	LUMIX_EXPECT(array1.empty());

	array1.resize(10);
	LUMIX_EXPECT(array1.size() == 10);

	array1.insert(0, 123);
	LUMIX_EXPECT(array1.size() == 11);
	LUMIX_EXPECT(array1[0] == 123);

	Lumix::Array<int> array2(allocator);
	array1.swap(array2);
	LUMIX_EXPECT(array2.size() == 11);
	LUMIX_EXPECT(array1.size() == 0);
}

void UT_array_erase(const char* params)
{
	Lumix::DefaultAllocator allocator;
	Lumix::Array<int> array1(allocator);

	for (int i = 0; i < 20; ++i)
	{
		array1.push(i * 5);
	}

	array1.eraseItem(25);
	LUMIX_EXPECT(array1.size() == 19);
	for (int i = 0; i < 18; ++i)
	{
		LUMIX_EXPECT(array1[i] < array1[i + 1]);
	}

	array1.erase(10);
	for (int i = 0; i < 17; ++i)
	{
		LUMIX_EXPECT(array1[i] < array1[i + 1]);
	}
	LUMIX_EXPECT(array1.size() == 18);

	array1.eraseFast(7);
	LUMIX_EXPECT(array1.size() == 17);

	array1.eraseItemFast(30);
	LUMIX_EXPECT(array1.size() == 16);

	array1.pop();
	LUMIX_EXPECT(array1.size() == 15);
}

void UT_array_move(const char* params)
{
	Lumix::DefaultAllocator allocator;
	Lumix::Array<int> array1(allocator);

	for (int i = 0; i < 20; ++i)
	{
		array1.push(i * 5);
	}

	Lumix::Array<int> array2 = Lumix::mymove(array1);
	LUMIX_EXPECT(array1.size() == 0);
	LUMIX_EXPECT(array1.capacity() == 0);
	LUMIX_EXPECT(array2.size() == 20);
}

namespace
{
class HasBackref
{
public:
	HasBackref()
		: m_backref(this)
	{
	}

	~HasBackref()
	{
		m_backref = nullptr;
	}

	HasBackref(const HasBackref& rhs)
		: m_backref(this)
	{
	}

	void operator=(const HasBackref& rhs)
	{
	}

	const HasBackref* backref() const { return m_backref; }

private:
	HasBackref* m_backref;
};
}

void UT_array_safeInsert(const char* params)
{
	Lumix::DefaultAllocator allocator;
	Lumix::Array<HasBackref> array(allocator);
	for (int i = 0; i < 10; ++i) {
		array.push(HasBackref());
	}
	for (int i = 0; i < 10; ++i) {
		LUMIX_EXPECT(&array[i] == array[i].backref());
	}
	array.insert(5, HasBackref());
	for (int i = 0; i < 10; ++i) {
		LUMIX_EXPECT(&array[i] == array[i].backref());
	}
}

REGISTER_TEST("unit_tests/core/array", UT_array, "")
REGISTER_TEST("unit_tests/core/array/erase", UT_array_erase, "")
REGISTER_TEST("unit_tests/core/array/move", UT_array_move, "")
REGISTER_TEST("unit_tests/core/array/safeInsert", UT_array_safeInsert, "")
