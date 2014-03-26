//
//  unit_test.h
//  UnitTest
//
//  Created by Lukas Jagelka on 14/03/14.
//  Copyright (c) 2014 LuxEngine. All rights reserved.
//

#pragma once

#include <string.h>

namespace Lux
{
	namespace UnitTest
	{
		template<typename T>
		LUX_FORCE_INLINE void expect(T p1, T p2, const char* file, uint32_t line)
		{
			if(p1 != p2)
			{
				Manager::instance().handleFail(file, line);
			}
		}

		template<>
		LUX_FORCE_INLINE void expect<const char*>(const char* p1, const char* p2, const char* file, uint32_t line)
		{
			if(strcmp(p1, p2) != 0)
			{
				Manager::instance().handleFail(file, line);
			}
		}

		LUX_FORCE_INLINE void expectTrue(bool b, const char* file, uint32_t line)
		{
			if(!b)
			{
				Manager::instance().handleFail(file, line);
			}
		}

		LUX_FORCE_INLINE void expectFalse(bool b, const char* file, uint32_t line)
		{
			if(b)
			{
				Manager::instance().handleFail(file, line);
			}
		}

		LUX_FORCE_INLINE void expectStringEq(const char* str1, const char* str2, const char* file, uint32_t line)
		{
			if(strcmp(str1, str2) != 0)
			{
				Manager::instance().handleFail(file, line);
			}
		}
	} // ~UnitTest
} // ~Lux

#define LUX_EXPECT(p1, p2)		Lux::UnitTest::expect(p1, p2, __FILE__, __LINE__)
#define LUX_EXPECT_TRUE(p1)		Lux::UnitTest::expectTrue(p1, __FILE__, __LINE__)
#define LUX_EXPECT_FALSE(p1)	Lux::UnitTest::expectFalse(p1, __FILE__, __LINE__)
#define LUX_EXPECT_STRING(p1, p2) Lux::UnitTest::expectStringEq(p1, p2, __FILE__, __LINE__)