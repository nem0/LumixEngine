#pragma once

#include "core/math_utils.h"
#include <string.h>

namespace Lumix
{
	namespace UnitTest
	{
		LUMIX_FORCE_INLINE void expect(bool b, const char* file, uint32 line)
		{
			if(!b)
			{
				Manager::instance().handleFail(file, line);
			}
		}
	} // ~UnitTest
} // ~Lumix

#define LUMIX_EXPECT(b)	Lumix::UnitTest::expect(b, __FILE__, __LINE__)
#define LUMIX_EXPECT_CLOSE_EQ(a, b, e)	Lumix::UnitTest::expect(((a) - (e)) < (b) && ((a) + (e)) > (b), __FILE__, __LINE__)
