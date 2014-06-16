#pragma once

namespace Lumix
{
	namespace UnitTest
	{
		struct App
		{
			void init();
			void run(int32_t argc, const char* argv[]);
			void exit();
		};
	} //~UnitTest
} //~UnitTest
