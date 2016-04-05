#pragma once

namespace Lumix
{
	namespace UnitTest
	{
		struct App
		{
			void init();
			void run(int32 argc, const char* argv[]);
			void exit();
		};
	} //~UnitTest
} //~UnitTest
