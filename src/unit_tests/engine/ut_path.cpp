#include "unit_tests/suite/lumix_unit_tests.h"

#include "engine/path.h"
#include "engine/crc32.h"
#include "engine/string.h"


using namespace Lumix;


const char src_path[] = "Unit\\Test\\PATH_1231231.EXT";
const char res_path[] = "unit/test/path_1231231.ext";

void UT_path(const char* params)
{
	DefaultAllocator allocator;
	PathManager path_manager(allocator);
	Path path(src_path);

	LUMIX_EXPECT(equalStrings(path.c_str(), res_path));

	LUMIX_EXPECT(path.getHash() == crc32(res_path));
}

REGISTER_TEST("unit_tests/engine/path/path", UT_path, "")
