#include "unit_tests/suite/lumix_unit_tests.h"
#include "engine/blob.h"
#include "engine/string.h"


void UT_blob(const char* params)
{
	Lumix::DefaultAllocator allocator;
	
	Lumix::OutputBlob blob(allocator);
	
	LUMIX_EXPECT(blob.getPos() == 0);
	bool b = false;
	blob.reserve(sizeof(b));
	LUMIX_EXPECT(blob.getPos() == 0);
	blob.write(b);
	LUMIX_EXPECT(blob.getPos() == sizeof(b));
	blob.reserve(sizeof(b));
	LUMIX_EXPECT(blob.getPos() == sizeof(b));

	char c = 'A';
	blob.reserve(sizeof(b) + sizeof(c));
	LUMIX_EXPECT(blob.getPos() == sizeof(b));
	blob.reserve(0);
	LUMIX_EXPECT(blob.getPos() == sizeof(b));
	blob.write(c);
	LUMIX_EXPECT(blob.getPos() == sizeof(b) + sizeof(c));

	int32 i = 123456;
	blob.write(i);

	uint32 ui = 0xABCDEF01;
	blob.write(ui);

	float f = Lumix::Math::PI;
	blob.write(f);

	blob.writeString("test string");

	struct S
	{
		int x;
		int y;
		char c;
	};
	S s;
	s.x = 1;
	s.y = 2;
	s.c = 'Q';
	blob.write(s);

	Lumix::InputBlob input(blob);
	bool b2;
	input.read(b2);

	char c2;
	input.read(c2);

	int32 i2;
	input.read(i2);

	uint32 ui2;
	input.read(ui2);

	float f2;
	input.read(f2);

	char tmp[20];
	input.readString(tmp, sizeof(tmp));

	S s2;
	input.read(s2);
	LUMIX_EXPECT(b == b2);
	LUMIX_EXPECT(c == c2);
	LUMIX_EXPECT(i == i2);
	LUMIX_EXPECT(ui == ui2);
	LUMIX_EXPECT(f == f2);
	LUMIX_EXPECT(Lumix::equalStrings(tmp, "test string"));
	LUMIX_EXPECT(Lumix::compareMemory(&s, &s2, sizeof(s)) == 0);

	input.rewind();
	input.read(b2);
	input.read(c2);
	input.read(i2);
	input.read(ui2);
	input.read(f2);
	input.readString(tmp, sizeof(tmp));
	input.read(s2);

	LUMIX_EXPECT(b == b2);
	LUMIX_EXPECT(c == c2);
	LUMIX_EXPECT(i == i2);
	LUMIX_EXPECT(ui == ui2);
	LUMIX_EXPECT(f == f2);
	LUMIX_EXPECT(Lumix::equalStrings(tmp, "test string"));
	LUMIX_EXPECT(Lumix::compareMemory(&s, &s2, sizeof(s)) == 0);

	LUMIX_EXPECT(input.getSize() == blob.getPos());
	input.setPosition(sizeof(b2) + sizeof(c2) + sizeof(i2));
	input.read(ui2);
	LUMIX_EXPECT(ui == ui2);

	blob.clear();
	LUMIX_EXPECT(blob.getPos() == 0);
	blob.write(b);
	LUMIX_EXPECT(blob.getPos() == sizeof(b));
}

REGISTER_TEST("unit_tests/engine/blob", UT_blob, "")
