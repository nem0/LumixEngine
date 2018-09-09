#include "unit_tests/suite/lumix_unit_tests.h"

#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_system.h"
#include "renderer/texture.h"


using namespace Lumix;


namespace
{

	void UT_texture_compareTGA(const char* params)
	{
		DefaultAllocator allocator;
		PathManager path_manager(allocator);

		FS::DiskFileDevice disk_file_device("disk", "", allocator);
		FS::IFile* file1 = disk_file_device.createFile(nullptr);
		FS::IFile* file2 = disk_file_device.createFile(nullptr);

		// if it fails somewhere here, check whether you have set working directory
		LUMIX_EXPECT(file1->open(Path("unit_tests/texture/1.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Path("unit_tests/texture/2.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Texture::compareTGA(file1, file2, 0, allocator) == 0);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Path("unit_tests/texture/1.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Path("unit_tests/texture/3.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Texture::compareTGA(file1, file2, 128, allocator) == 51*51);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Path("unit_tests/texture/1.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Path("unit_tests/texture/4.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Texture::compareTGA(file1, file2, 1, allocator) == 512 * 512 >> 1);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Path("unit_tests/texture/1.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Path("unit_tests/texture/5.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Texture::compareTGA(file1, file2, 250, allocator) == 512*512);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Path("unit_tests/texture/6.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Path("unit_tests/texture/7.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Texture::compareTGA(file1, file2, 0, allocator) == 512*512);

		file1->close();
		file2->close();
		LUMIX_EXPECT(file1->open(Path("unit_tests/texture/6.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(file2->open(Path("unit_tests/texture/8.tga"), FS::Mode::OPEN_AND_READ));
		LUMIX_EXPECT(Texture::compareTGA(file1, file2, 0, allocator) == 416);

		file1->close();
		file2->close();
		disk_file_device.destroyFile(file1);
		disk_file_device.destroyFile(file2);
	}

	REGISTER_TEST("unit_tests/graphics/texture/compareTGA", UT_texture_compareTGA, "");

}
