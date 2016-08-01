#include "engine/lumix.h"
#include "engine/debug/floating_points.h"
#include "unit_tests/suite/unit_test_app.h"


int main(int argc, const char * argv[])
{
	Lumix::enableFloatingPointTraps(true);
	Lumix::UnitTest::App app;
	app.init();
	app.run(argc, argv);
	app.exit();
}

