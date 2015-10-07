#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/command_line_parser.h"


void UT_command_line_parser(const char* params)
{
	Lumix::CommandLineParser parser("-x 10 -y 20\t-plugin  custom.dll -str \"test\" -str2 \"test with spaces\"");

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("-x"));
	LUMIX_EXPECT_FALSE(parser.currentEquals("-y"));
	LUMIX_EXPECT_FALSE(parser.currentEquals("-"));
	LUMIX_EXPECT_FALSE(parser.currentEquals(""));
	LUMIX_EXPECT_FALSE(parser.currentEquals("10"));

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("10"));

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("-y"));

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("20"));

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("-plugin"));

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("custom.dll"));

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("-str"));

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("\"test\""));

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("-str2"));

	LUMIX_EXPECT_TRUE(parser.next());
	LUMIX_EXPECT_TRUE(parser.currentEquals("\"test with spaces\""));

	LUMIX_EXPECT_FALSE(parser.next());

	Lumix::CommandLineParser parser2("");
	LUMIX_EXPECT_FALSE(parser2.next());

	Lumix::CommandLineParser parser3("  ");
	LUMIX_EXPECT_FALSE(parser3.next());

	Lumix::CommandLineParser parser4("\t");
	LUMIX_EXPECT_FALSE(parser4.next());

	Lumix::CommandLineParser parser5("\"\"");
	LUMIX_EXPECT_TRUE(parser5.next());
	LUMIX_EXPECT_TRUE(parser5.currentEquals("\"\""));
	LUMIX_EXPECT_FALSE(parser5.next());

	Lumix::CommandLineParser parser6(" \" \" ");
	LUMIX_EXPECT_TRUE(parser6.next());
	LUMIX_EXPECT_TRUE(parser6.currentEquals("\" \""));
	LUMIX_EXPECT_FALSE(parser6.next());
}

REGISTER_TEST("unit_tests/core/command_line_parser", UT_command_line_parser, "")