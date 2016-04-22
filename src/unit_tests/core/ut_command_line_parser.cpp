#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/core/command_line_parser.h"


void UT_command_line_parser(const char* params)
{
	char tmp[100];
	Lumix::CommandLineParser parser("-x 10 -y 20\t-plugin  custom.dll -str \"test\" -str2 \"test with spaces\"");

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("-x"));
	LUMIX_EXPECT(!parser.currentEquals("-y"));
	LUMIX_EXPECT(!parser.currentEquals("-"));
	LUMIX_EXPECT(!parser.currentEquals(""));
	LUMIX_EXPECT(!parser.currentEquals("10"));

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("10"));

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("-y"));

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("20"));

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("-plugin"));

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("custom.dll"));
	parser.getCurrent(tmp, Lumix::lengthOf(tmp));
	LUMIX_EXPECT(Lumix::compareString(tmp, "custom.dll") == 0);

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("-str"));

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("\"test\""));
	parser.getCurrent(tmp, Lumix::lengthOf(tmp));
	LUMIX_EXPECT(Lumix::compareString(tmp, "test") == 0);

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("-str2"));

	LUMIX_EXPECT(parser.next());
	LUMIX_EXPECT(parser.currentEquals("\"test with spaces\""));
	parser.getCurrent(tmp, Lumix::lengthOf(tmp));
	LUMIX_EXPECT(Lumix::compareString(tmp, "test with spaces") == 0);


	LUMIX_EXPECT(!parser.next());

	Lumix::CommandLineParser parser2("");
	LUMIX_EXPECT(!parser2.next());

	Lumix::CommandLineParser parser3("  ");
	LUMIX_EXPECT(!parser3.next());

	Lumix::CommandLineParser parser4("\t");
	LUMIX_EXPECT(!parser4.next());

	Lumix::CommandLineParser parser5("\"\"");
	LUMIX_EXPECT(parser5.next());
	LUMIX_EXPECT(parser5.currentEquals("\"\""));
	LUMIX_EXPECT(!parser5.next());

	Lumix::CommandLineParser parser6(" \" \" ");
	LUMIX_EXPECT(parser6.next());
	parser6.getCurrent(tmp, Lumix::lengthOf(tmp));
	LUMIX_EXPECT(Lumix::compareString(tmp, " ") == 0);
	LUMIX_EXPECT(parser6.currentEquals("\" \""));
	LUMIX_EXPECT(!parser6.next());
}

REGISTER_TEST("unit_tests/core/command_line_parser", UT_command_line_parser, "")