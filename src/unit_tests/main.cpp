#include "core/lumix.h"
#include <iostream>
#include "unit_tests/suite/unit_test_app.h"

int main(int argc, const char * argv[])
{
	Lumix::UnitTest::App app;
	app.init();
	app.run(argc, argv);
	app.exit();
}

