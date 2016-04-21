#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/fs/disk_file_device.h"
#include "core/fs/file_system.h"
#include "renderer/texture.h"

namespace
{

	void UT_texture_compareTGA(const char* params)
	{
		Lumix::DefaultAllocator allocator;
		Lumix::PathManager path_manager(allocator);

		Lumix::FS::DiskFileDevice disk_file_device("disk", "", allocator);
		Lumix::FS::IFile* file1 = disk_file_device.createFile(NULL);
		Lumix::FS::IFile* file2 = disk_file_device.createFile(NULL);

		// if it fails somewhere here, check whether you have set working directory
		LUMIX_EXPECT(file1->open(Lumix::Path("unit_tests/texture/1.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Lumix::Path("unit_tests/texture/2.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Lumix::Texture::compareTGA(allocator, file1, file2, 0) == 0);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Lumix::Path("unit_tests/texture/1.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Lumix::Path("unit_tests/texture/3.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Lumix::Texture::compareTGA(allocator, file1, file2, 128) == 51*51);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Lumix::Path("unit_tests/texture/1.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Lumix::Path("unit_tests/texture/4.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Lumix::Texture::compareTGA(allocator, file1, file2, 1) == 512 * 512 >> 1);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Lumix::Path("unit_tests/texture/1.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Lumix::Path("unit_tests/texture/5.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Lumix::Texture::compareTGA(allocator, file1, file2, 250) == 512*512);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Lumix::Path("unit_tests/texture/6.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Lumix::Path("unit_tests/texture/7.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Lumix::Texture::compareTGA(allocator, file1, file2, 0) == 512*512);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Lumix::Path("unit_tests/texture/6.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Lumix::Path("unit_tests/texture/8.tga"), Lumix::FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Lumix::Texture::compareTGA(allocator, file1, file2, 0) == 416);

		file1->close();
		file2->close();
		disk_file_device.destroyFile(file1);
		disk_file_device.destroyFile(file2);
	}

	REGISTER_TEST("unit_tests/graphics/texture/compareTGA", UT_texture_compareTGA, "");

}
