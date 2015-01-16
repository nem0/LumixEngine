#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/blob.h"


void UT_blob(const char* params)
{
	Lumix::DefaultAllocator allocator;
	/*
	Lumix::Blob blob(allocator);
	char data[] = "abcdef";
	blob.create(data, sizeof(data));
	blob.
	*/

	// TODO refactor blob itself
	ASSERT(false); // TODO 
}

REGISTER_TEST("unit_tests/core/blob", UT_blob, "")