#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/fs/ifile.h"
#include "core/FS/memory_file_device.h"
#include "core/json_serializer.h"
#include "core/path.h"
#include <cstdio>


void UT_json_serializer(const char* params)
{
	Lumix::DefaultAllocator allocator;
	Lumix::PathManager path_manager(allocator);

	Lumix::FS::MemoryFileDevice device(allocator);
	Lumix::FS::IFile* file = device.createFile(NULL);
	{
		Lumix::JsonSerializer serializer(*file, Lumix::JsonSerializer::WRITE, Lumix::Path(""), allocator);
		serializer.beginObject();

		serializer.beginArray("array");
		serializer.serializeArrayItem(10);
		serializer.serializeArrayItem(20);
		serializer.serializeArrayItem(30);
		serializer.endArray();

		serializer.beginObject("subobject");
		serializer.serialize("bool", false);
		serializer.serialize("int", 1);
		serializer.serialize("float", 2.0f);
		serializer.serialize("const_char", "some string");
		serializer.endObject();

		serializer.endObject();
	}
	
	file->seek(Lumix::FS::SeekMode::BEGIN, 0);

	{
		Lumix::JsonSerializer serializer(*file, Lumix::JsonSerializer::READ, Lumix::Path(""), allocator);
		serializer.deserializeObjectBegin();

		LUMIX_EXPECT(!serializer.isObjectEnd());
		LUMIX_EXPECT(!serializer.isArrayEnd());

		serializer.deserializeArrayBegin("array");
		LUMIX_EXPECT(!serializer.isObjectEnd());
		int ar[3];
		serializer.deserializeArrayItem(ar[0], -1);
		LUMIX_EXPECT(!serializer.isObjectEnd());
		LUMIX_EXPECT(!serializer.isArrayEnd());
		serializer.deserializeArrayItem(ar[1], -1);
		LUMIX_EXPECT(!serializer.isObjectEnd());
		LUMIX_EXPECT(!serializer.isArrayEnd());
		serializer.deserializeArrayItem(ar[2], -1);
		LUMIX_EXPECT(ar[0] == 10);
		LUMIX_EXPECT(ar[1] == 20);
		LUMIX_EXPECT(ar[2] == 30);
		LUMIX_EXPECT(serializer.isArrayEnd());
		
		serializer.deserializeArrayEnd();

		char label[50];
		serializer.deserializeLabel(label, sizeof(label));
		serializer.deserializeObjectBegin();
		bool b;
		serializer.deserialize("bool", b, true);
		LUMIX_EXPECT(!b);

		int i;
		serializer.deserialize("int", i, -1);
		LUMIX_EXPECT(i == 1);

		float f;
		serializer.deserialize("float", f, -1);
		LUMIX_EXPECT(f == 2);

		char str[100];
		serializer.deserialize("const_char", str, sizeof(str), "");
		LUMIX_EXPECT(Lumix::compareString(str , "some string") == 0);
		LUMIX_EXPECT(serializer.isObjectEnd());

		serializer.deserializeObjectEnd();

		LUMIX_EXPECT(serializer.isObjectEnd());

		serializer.deserializeObjectEnd();

		LUMIX_EXPECT(!serializer.isError());
	}

	device.destroyFile(file);
}

REGISTER_TEST("unit_tests/core/json_serializer", UT_json_serializer, "")