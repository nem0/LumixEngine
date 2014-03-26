//
//  main.cpp
//  UnitTest
//
//  Created by Lukas Jagelka on 14/03/14.
//  Copyright (c) 2014 LuxEngine. All rights reserved.
//

#include "core/lux.h"
#include <iostream>
#include "unit_tests/unit_test_app.h"

int main(int argc, const char * argv[])
{
	Lux::UnitTest::App app;
	app.init();
	app.run(argc, argv);
	app.exit();
}

