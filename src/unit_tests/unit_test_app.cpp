#include "core/lux.h"
#include "unit_tests/platform_defines.h"
#include "unit_tests/unit_test_app.h"
#include "unit_tests/unit_test_manager.h"

#include <Windows.h>
#include <stdio.h>

#include "core/log.h"

namespace Lux
{
	namespace UnitTest
	{
		void outputToVS(const char* system, const char* message)
		{
			char tmp[2048];
			sprintf(tmp, "%s: %s\r", system, message);

			OutputDebugString(tmp);
		}

		void App::init()
		{
			g_log_info.getCallback().bind<outputToVS>();
			g_log_warning.getCallback().bind<outputToVS>();
			g_log_error.getCallback().bind<outputToVS>();
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