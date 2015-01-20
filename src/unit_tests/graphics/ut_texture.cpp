#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/FS/disk_file_device.h"
#include "core/FS/ifile.h"
#include "graphics/texture.h"

namespace
{

	void UT_texture_compareTGA(const char* params)
	{
		Lumix::DefaultAllocator allocator;

		Lumix::FS::DiskFileDevice disk_file_device(allocator);
		Lumix::FS::IFile* file1 = disk_file_device.createFile(NULL);
		Lumix::FS::IFile* file2 = disk_file_device.createFile(NULL);

		file1->open("unit_tests/texture/1.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		file2->open("unit_tests/texture/2.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		LUMIX_EXPECT_CLOSE_EQ(Lumix::Texture::compareTGA(allocator, file1, file2), 1, 0.001f);

		file1->close();
		file2->close();
		file1->open("unit_tests/texture/1.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		file2->open("unit_tests/texture/3.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		LUMIX_EXPECT_CLOSE_EQ(Lumix::Texture::compareTGA(allocator, file1, file2), 0.99f, 0.001f);

		file1->close();
		file2->close();
		file1->open("unit_tests/texture/1.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		file2->open("unit_tests/texture/4.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		LUMIX_EXPECT_CLOSE_EQ(Lumix::Texture::compareTGA(allocator, file1, file2), 0.5f, 0.001f);

		file1->close();
		file2->close();
		file1->open("unit_tests/texture/1.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		file2->open("unit_tests/texture/5.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		LUMIX_EXPECT_CLOSE_EQ(Lumix::Texture::compareTGA(allocator, file1, file2), 0, 0.001f);

		file1->close();
		file2->close();
		file1->open("unit_tests/texture/6.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		file2->open("unit_tests/texture/7.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		LUMIX_EXPECT_CLOSE_EQ(Lumix::Texture::compareTGA(allocator, file1, file2), 0.45355f, 0.001f);

		file1->close();
		file2->close();
		file1->open("unit_tests/texture/6.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		file2->open("unit_tests/texture/8.tga", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
		LUMIX_EXPECT_CLOSE_EQ(Lumix::Texture::compareTGA(allocator, file1, file2), 0.9998f, 0.001f);

		file1->close();
		file2->close();
		disk_file_device.destroyFile(file1);
		disk_file_device.destroyFile(file2);
	}

	REGISTER_TEST("unit_tests/graphics/texture/compareTGA", UT_texture_compareTGA, "");

}
