#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/crc32.h"


using namespace Lumix;


void UT_crc32(const char* params)
{
	DefaultAllocator allocator;

	LUMIX_EXPECT(crc32("123456789") == 0xCBF43926);
	LUMIX_EXPECT(crc32("LumixEngine") == 0x447C892F);
	LUMIX_EXPECT(crc32("    ") == 0x17D132A8);
	LUMIX_EXPECT(crc32("test") == crc32("test"));
	LUMIX_EXPECT(crc32("test") != crc32("TEST"));
	LUMIX_EXPECT(crc32("test") != crc32("Test"));
	LUMIX_EXPECT(crc32("test") != crc32("1234"));
	LUMIX_EXPECT(crc32("\x01") == 0xA505DF1B);
	LUMIX_EXPECT(crc32("\xff") == 0xFF000000);
	LUMIX_EXPECT(crc32("\xff\xff") == 0xFFFF0000);
	LUMIX_EXPECT(crc32("\xff\xff\x12") == 0x214461C5);
}

REGISTER_TEST("unit_tests/engine/crc32", UT_crc32, "")
