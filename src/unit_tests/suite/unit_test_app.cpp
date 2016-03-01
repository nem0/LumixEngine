#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/log.h"
#include "core/pc/simple_win.h"
#include "core/stack_allocator.h"
#include <cstdio>


namespace Lumix
{
	namespace UnitTest
	{
		void outputToVS(const char* system, const char* message)
		{
			StackAllocator<2048> allocator;
			string tmp(allocator);
			tmp = system;
			tmp += ": ";
			tmp += message;
			tmp += "\r";

			OutputDebugString(tmp.c_str());
		}

		void outputToConsole(const char* system, const char* message)
		{
			printf("%s: %s\n", system, message);
		}

		void App::init()
		{
			g_log_info.getCallback().bind<outputToVS>();
			g_log_warning.getCallback().bind<outputToVS>();
			g_log_error.getCallback().bind<outputToVS>();

			g_log_info.getCallback().bind<outputToConsole>();
			g_log_warning.getCallback().bind<outputToConsole>();
			g_log_error.getCallback().bind<outputToConsole>();
		}

		void App::run(int argc, const char *argv[])
		{
			Manager::instance().dumpTests();
			Manager::instance().runTests("*");
			Manager::instance().dumpResults();
		}

		void App::exit()
		{
			Manager::release();
		}
	} //~UnitTest
} //~UnitTest