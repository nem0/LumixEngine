#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/path.h"
#include "core/crc32.h"
#include "core/string.h"

const char src_path[] = "Unit\\Test\\PATH_1231231.EXT";
const char res_path[] = "unit/test/path_1231231.ext";

void UT_path(const char* params)
{
	Lumix::DefaultAllocator allocator;
	Lumix::PathManager path_manager(allocator);
	Lumix::Path path(src_path);

	LUMIX_EXPECT(Lumix::compareString(path.c_str(), res_path) == 0);

	LUMIX_EXPECT(path.getHash() == Lumix::crc32(res_path));
}

REGISTER_TEST("unit_tests/core/path/path", UT_path, "")