#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/core/delegate.h"

static int x = 0;

void test()
{
	x = 10;
}

void test2(int r)
{
	x = r;
}


struct S
{
	void test()
	{
		m = 10;
	}

	void test2(int r)
	{
		m = r;
	}

	int m;
};


void UT_delegate(const char* params)
{
	Lumix::DefaultAllocator allocator;

	Lumix::Delegate<void> d1;

	d1.bind<&test>();
	d1.invoke();

	LUMIX_EXPECT(x == 10);

	Lumix::Delegate<void (int)> d2;
	d2.bind<&test2>();
	d2.invoke(20);
	LUMIX_EXPECT(x == 20);

	S s;
	d1.bind<S, &S::test>(&s);
	d1.invoke();
	LUMIX_EXPECT(x == 20);
	LUMIX_EXPECT(s.m == 10);

	d2.bind<S, &S::test2>(&s);
	d2.invoke(30);
	LUMIX_EXPECT(x == 20);
	LUMIX_EXPECT(s.m == 30);
}

REGISTER_TEST("unit_tests/core/delegate", UT_delegate, "")