#include "unit_tests/suite/lumix_unit_tests.h"

#include <Windows.h>
#include <stdio.h>

#include "core/log.h"

namespace Lumix
{
	namespace UnitTest
	{
		void outputToVS(const char* system, const char* message)
		{
			char tmp[2048];
			sprintf(tmp, "%s: %s\r", system, message);

			OutputDebugString(tmp);
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
			Manager::instance().runTests("");
			Manager::instance().dumpResults();
		}

		void App::exit()
		{
			Manager::release();
		}
	} //~UnitTest
} //~UnitTest