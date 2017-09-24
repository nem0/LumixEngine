#include "unit_tests/suite/lumix_unit_tests.h"

#include "engine/path_utils.h"


using namespace Lumix;



void UT_path_utils(const char* params)
{
	char tmp[MAX_PATH_LENGTH];
	PathUtils::getFilename(tmp, lengthOf(tmp), "filename.ext");
	LUMIX_EXPECT(equalIStrings(tmp, "filename.ext"));
	PathUtils::getFilename(tmp, lengthOf(tmp), "/filename.ext");
	LUMIX_EXPECT(equalIStrings(tmp, "filename.ext"));
	PathUtils::getFilename(tmp, lengthOf(tmp), "dir/filename.ext");
	LUMIX_EXPECT(equalIStrings(tmp, "filename.ext"));
	PathUtils::getFilename(tmp, lengthOf(tmp), "/dir/filename.ext");
	LUMIX_EXPECT(equalIStrings(tmp, "filename.ext"));
	PathUtils::getFilename(tmp, lengthOf(tmp), "/long/path/dir/filename.ext");
	LUMIX_EXPECT(equalIStrings(tmp, "filename.ext"));

	PathUtils::getBasename(tmp, lengthOf(tmp), "filename.ext");
	LUMIX_EXPECT(equalIStrings(tmp, "filename"));

	PathUtils::getBasename(tmp, lengthOf(tmp), "/filename.ext");
	LUMIX_EXPECT(equalIStrings(tmp, "filename"));
	PathUtils::getBasename(tmp, lengthOf(tmp), "/filename");
	LUMIX_EXPECT(equalIStrings(tmp, "filename"));
	PathUtils::getBasename(tmp, lengthOf(tmp), "filename");
	LUMIX_EXPECT(equalIStrings(tmp, "filename"));
	PathUtils::getBasename(tmp, lengthOf(tmp), "dir/filename");
	LUMIX_EXPECT(equalIStrings(tmp, "filename"));
	PathUtils::getBasename(tmp, lengthOf(tmp), "/dir/filename");
	LUMIX_EXPECT(equalIStrings(tmp, "filename"));
	PathUtils::getBasename(tmp, lengthOf(tmp), "/long/path/dir/filename");
	LUMIX_EXPECT(equalIStrings(tmp, "filename"));
}

REGISTER_TEST("unit_tests/engine/path_utils/path_path_utils", UT_path_utils, "")
