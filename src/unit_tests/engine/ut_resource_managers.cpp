#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/fs/ifile.h"
#include "core/fs/file_system.h"
#include "core/fs/memory_file_device.h"
#include "core/fs/disk_file_device.h"

#include "core/mt/thread.h"

#include "core/resource_manager.h"
#include "core/resource.h"

#include "graphics/texture_manager.h"
#include "graphics/texture.h"

#include "animation/animation.h"

namespace
{
	const char texture_test_tga[] = "unit_tests/resource_managers/cisla.tga";
	const size_t texture_test_tga_size = 262188;
	const char texture_test_dds[] = "unit_tests/resource_managers/trava3.dds";
	const char texture_test_failure[] = "unit_tests/resource_managers/_non_exist.dds";

	void waitForFinishLoading(Lumix::Resource* resource, Lumix::FS::FileSystem* file_system)
	{
		do
		{
			file_system->updateAsyncTransactions();
			Lumix::MT::yield();
		} while (resource->isLoading());
	}

	void UT_material_manager(const char* params)
	{
		Lumix::FS::FileSystem* file_system = Lumix::FS::FileSystem::create();

		Lumix::FS::MemoryFileDevice mem_file_device;
		Lumix::FS::DiskFileDevice disk_file_device;

		file_system->mount(&mem_file_device);
		file_system->mount(&disk_file_device);
		file_system->setDefaultDevice("memory:disk");

		Lumix::ResourceManager resource_manager;
		Lumix::DefaultAllocator allocator;
		Lumix::TextureManager texture_manager(allocator);
		resource_manager.create(*file_system);
		texture_manager.create(Lumix::ResourceManager::TEXTURE, resource_manager);

		Lumix::g_log_info.log("unit") << "loading ...";
		Lumix::Resource* texture_tga1 = texture_manager.load(texture_test_tga);
		Lumix::Resource* texture_tga2 = texture_manager.load(texture_test_tga);
		Lumix::Resource* texture_tga3 = texture_manager.get(texture_test_tga);

		LUMIX_EXPECT_NOT_NULL(texture_tga1);
		LUMIX_EXPECT_NOT_NULL(texture_tga2);
		LUMIX_EXPECT_NOT_NULL(texture_tga3);

		LUMIX_EXPECT_EQ(texture_tga1, texture_tga2);
		LUMIX_EXPECT_EQ(texture_tga2, texture_tga3);

		LUMIX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUMIX_EXPECT_TRUE(texture_tga1->isLoading());
		LUMIX_EXPECT_FALSE(texture_tga1->isReady());
		LUMIX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUMIX_EXPECT_FALSE(texture_tga1->isFailure());

		LUMIX_EXPECT_EQ(0, texture_tga1->size());

		waitForFinishLoading(texture_tga1, file_system);

		LUMIX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUMIX_EXPECT_FALSE(texture_tga1->isLoading());
		LUMIX_EXPECT_TRUE(texture_tga1->isReady());
		LUMIX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUMIX_EXPECT_FALSE(texture_tga1->isFailure());

		LUMIX_EXPECT_EQ(texture_test_tga_size, texture_tga1->size());

		Lumix::g_log_info.log("unit") << "unloading ...";

		texture_manager.unload(texture_test_tga);

		LUMIX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUMIX_EXPECT_FALSE(texture_tga1->isLoading());
		LUMIX_EXPECT_TRUE(texture_tga1->isReady());
		LUMIX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUMIX_EXPECT_FALSE(texture_tga1->isFailure());

		texture_manager.unload(*texture_tga2);

		// Should start unloading. The get method doesn't count references.
		LUMIX_EXPECT_TRUE(texture_tga1->isEmpty());
		LUMIX_EXPECT_FALSE(texture_tga1->isLoading());
		LUMIX_EXPECT_FALSE(texture_tga1->isReady());
		LUMIX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUMIX_EXPECT_FALSE(texture_tga1->isFailure());

		LUMIX_EXPECT_EQ(0, texture_tga1->size());

		Lumix::g_log_info.log("unit") << "loading ...";

		texture_manager.load(*texture_tga1);
		texture_manager.load(*texture_tga2);
		texture_manager.load(*texture_tga3);

		LUMIX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUMIX_EXPECT_TRUE(texture_tga1->isLoading());
		LUMIX_EXPECT_FALSE(texture_tga1->isReady());
		LUMIX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUMIX_EXPECT_FALSE(texture_tga1->isFailure());

		waitForFinishLoading(texture_tga1, file_system);

		LUMIX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUMIX_EXPECT_FALSE(texture_tga1->isLoading());
		LUMIX_EXPECT_TRUE(texture_tga1->isReady());
		LUMIX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUMIX_EXPECT_FALSE(texture_tga1->isFailure());

		LUMIX_EXPECT_EQ(texture_test_tga_size, texture_tga1->size());


		Lumix::g_log_info.log("unit") << "force unloading ...";

		texture_manager.forceUnload(texture_test_tga);

		LUMIX_EXPECT_TRUE(texture_tga1->isEmpty());
		LUMIX_EXPECT_FALSE(texture_tga1->isLoading());
		LUMIX_EXPECT_FALSE(texture_tga1->isReady());
		LUMIX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUMIX_EXPECT_FALSE(texture_tga1->isFailure());

		LUMIX_EXPECT_EQ(0, texture_tga1->size());

		Lumix::Resource* texture_fail = texture_manager.load(texture_test_failure);

		LUMIX_EXPECT_NOT_NULL(texture_fail);

		LUMIX_EXPECT_FALSE(texture_fail->isEmpty());
		LUMIX_EXPECT_TRUE(texture_fail->isLoading());
		LUMIX_EXPECT_FALSE(texture_fail->isReady());
		LUMIX_EXPECT_FALSE(texture_fail->isUnloading());
		LUMIX_EXPECT_FALSE(texture_fail->isFailure());

		LUMIX_EXPECT_EQ(0, texture_fail->size());

		waitForFinishLoading(texture_fail, file_system);

		LUMIX_EXPECT_FALSE(texture_fail->isEmpty());
		LUMIX_EXPECT_FALSE(texture_fail->isLoading());
		LUMIX_EXPECT_FALSE(texture_fail->isReady());
		LUMIX_EXPECT_FALSE(texture_fail->isUnloading());
		LUMIX_EXPECT_TRUE(texture_fail->isFailure());

		// exit
		texture_manager.releaseAll();
		texture_manager.destroy();
		resource_manager.destroy();

		file_system->unMount(&disk_file_device);
		file_system->unMount(&mem_file_device);

		Lumix::FS::FileSystem::destroy(file_system);
	}

	const char anim_test[] = "unit_tests/resource_managers/blender.ani";
	const size_t anim_test_size = 65872;
	const char anim_test_failure[] = "unit_tests/resource_managers/_non_exist.dds";

	void UT_animation_manager(const char* params)
	{
		Lumix::FS::FileSystem* file_system = Lumix::FS::FileSystem::create();

		Lumix::FS::MemoryFileDevice mem_file_device;
		Lumix::FS::DiskFileDevice disk_file_device;

		file_system->mount(&mem_file_device);
		file_system->mount(&disk_file_device);
		file_system->setDefaultDevice("memory:disk");

		Lumix::ResourceManager resource_manager;
		Lumix::DefaultAllocator allocator;
		Lumix::AnimationManager animation_manager(allocator);
		resource_manager.create(*file_system);
		animation_manager.create(Lumix::ResourceManager::ANIMATION, resource_manager);

		Lumix::g_log_info.log("unit") << "loading ...";
		Lumix::Resource* animation_1 = animation_manager.load(anim_test);
		Lumix::Resource* animation_2 = animation_manager.get(anim_test);

		LUMIX_EXPECT_NOT_NULL(animation_1);
		LUMIX_EXPECT_NOT_NULL(animation_2);

		LUMIX_EXPECT_EQ(animation_1, animation_2);

		LUMIX_EXPECT_FALSE(animation_1->isEmpty());
		LUMIX_EXPECT_TRUE(animation_1->isLoading());
		LUMIX_EXPECT_FALSE(animation_1->isReady());
		LUMIX_EXPECT_FALSE(animation_1->isUnloading());
		LUMIX_EXPECT_FALSE(animation_1->isFailure());

		LUMIX_EXPECT_EQ(0, animation_1->size());

		waitForFinishLoading(animation_1, file_system);

		LUMIX_EXPECT_FALSE(animation_2->isEmpty());
		LUMIX_EXPECT_FALSE(animation_2->isLoading());
		LUMIX_EXPECT_TRUE(animation_2->isReady());
		LUMIX_EXPECT_FALSE(animation_2->isUnloading());
		LUMIX_EXPECT_FALSE(animation_2->isFailure());

		LUMIX_EXPECT_EQ(anim_test_size, animation_2->size());

		Lumix::g_log_info.log("unit") << "unloading ...";

		animation_manager.unload(*animation_2);

		// Should start unloading. The get method doesn't count references.
		LUMIX_EXPECT_TRUE(animation_1->isEmpty());
		LUMIX_EXPECT_FALSE(animation_1->isLoading());
		LUMIX_EXPECT_FALSE(animation_1->isReady());
		LUMIX_EXPECT_FALSE(animation_1->isUnloading());
		LUMIX_EXPECT_FALSE(animation_1->isFailure());

		LUMIX_EXPECT_EQ(0, animation_1->size());

		Lumix::g_log_info.log("unit") << "loading ...";

		animation_manager.load(*animation_1);
		animation_manager.load(*animation_2);

		LUMIX_EXPECT_FALSE(animation_1->isEmpty());
		LUMIX_EXPECT_TRUE(animation_1->isLoading());
		LUMIX_EXPECT_FALSE(animation_1->isReady());
		LUMIX_EXPECT_FALSE(animation_1->isUnloading());
		LUMIX_EXPECT_FALSE(animation_1->isFailure());

		waitForFinishLoading(animation_1, file_system);

		LUMIX_EXPECT_FALSE(animation_1->isEmpty());
		LUMIX_EXPECT_FALSE(animation_1->isLoading());
		LUMIX_EXPECT_TRUE(animation_1->isReady());
		LUMIX_EXPECT_FALSE(animation_1->isUnloading());
		LUMIX_EXPECT_FALSE(animation_1->isFailure());

		LUMIX_EXPECT_EQ(anim_test_size, animation_1->size());

		Lumix::g_log_info.log("unit") << "force unloading ...";

		animation_manager.forceUnload(*animation_2);

		LUMIX_EXPECT_TRUE(animation_2->isEmpty());
		LUMIX_EXPECT_FALSE(animation_2->isLoading());
		LUMIX_EXPECT_FALSE(animation_2->isReady());
		LUMIX_EXPECT_FALSE(animation_2->isUnloading());
		LUMIX_EXPECT_FALSE(animation_2->isFailure());

		LUMIX_EXPECT_EQ(0, animation_2->size());

		Lumix::Resource* animation_fail = animation_manager.load(anim_test_failure);

		LUMIX_EXPECT_NOT_NULL(animation_fail);

		LUMIX_EXPECT_FALSE(animation_fail->isEmpty());
		LUMIX_EXPECT_TRUE(animation_fail->isLoading());
		LUMIX_EXPECT_FALSE(animation_fail->isReady());
		LUMIX_EXPECT_FALSE(animation_fail->isUnloading());
		LUMIX_EXPECT_FALSE(animation_fail->isFailure());

		LUMIX_EXPECT_EQ(0, animation_fail->size());

		waitForFinishLoading(animation_fail, file_system);

		LUMIX_EXPECT_FALSE(animation_fail->isEmpty());
		LUMIX_EXPECT_FALSE(animation_fail->isLoading());
		LUMIX_EXPECT_FALSE(animation_fail->isReady());
		LUMIX_EXPECT_FALSE(animation_fail->isUnloading());
		LUMIX_EXPECT_TRUE(animation_fail->isFailure());

		// exit
		animation_manager.releaseAll();
		animation_manager.destroy();
		resource_manager.destroy();

		file_system->unMount(&disk_file_device);
		file_system->unMount(&mem_file_device);

		Lumix::FS::FileSystem::destroy(file_system);
	}

	const char anim_test_valid[] = "unit_tests/resource_managers/blender.ani";
	const char anim_test_fail[] = "unit_tests/resource_managers/failure.ani";
	const char anim_test_invalid[] = "unit_tests/resource_managers/cisla.tga";

	uint8_t buffer[512 * 1024 * 1024];

	void UT_failure_reload(const char* params)
	{
		Lumix::FS::FileSystem* file_system = Lumix::FS::FileSystem::create();

		Lumix::FS::MemoryFileDevice mem_file_device;
		Lumix::FS::DiskFileDevice disk_file_device;

		file_system->mount(&mem_file_device);
		file_system->mount(&disk_file_device);
		file_system->setDefaultDevice("memory:disk");

		Lumix::ResourceManager resource_manager;
		Lumix::DefaultAllocator allocator;
		Lumix::AnimationManager animation_manager(allocator);
		resource_manager.create(*file_system);
		animation_manager.create(Lumix::ResourceManager::ANIMATION, resource_manager);

		Lumix::g_log_info.log("unit") << "loading ...";
		{
			Lumix::FS::IFile* valid_file = file_system->open("memory:disk", anim_test_valid, Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
			LUMIX_EXPECT_NOT_NULL(valid_file);

			Lumix::FS::IFile* error_file = file_system->open("memory:disk", anim_test_fail, Lumix::FS::Mode::OPEN_OR_CREATE | Lumix::FS::Mode::WRITE);
			LUMIX_EXPECT_NOT_NULL(error_file);

			size_t size = valid_file->size();
			valid_file->read(buffer, size);
			error_file->write(buffer, size);

			file_system->close(valid_file);
			file_system->close(error_file);
		}

		Lumix::g_log_info.log("unit") << "loading ...";
		Lumix::Resource* animation = animation_manager.load(anim_test_fail);

		LUMIX_EXPECT_NOT_NULL(animation);

		LUMIX_EXPECT_FALSE(animation->isEmpty());
		LUMIX_EXPECT_TRUE(animation->isLoading());
		LUMIX_EXPECT_FALSE(animation->isReady());
		LUMIX_EXPECT_FALSE(animation->isUnloading());
		LUMIX_EXPECT_FALSE(animation->isFailure());

		LUMIX_EXPECT_EQ(0, animation->size());

		waitForFinishLoading(animation, file_system);

		LUMIX_EXPECT_FALSE(animation->isEmpty());
		LUMIX_EXPECT_FALSE(animation->isLoading());
		LUMIX_EXPECT_TRUE(animation->isReady());
		LUMIX_EXPECT_FALSE(animation->isUnloading());
		LUMIX_EXPECT_FALSE(animation->isFailure());

		{
			Lumix::FS::IFile* invalid_file = file_system->open("memory:disk", anim_test_invalid, Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
			LUMIX_EXPECT_NOT_NULL(invalid_file);

			Lumix::FS::IFile* error_file = file_system->open("memory:disk", anim_test_fail, Lumix::FS::Mode::OPEN_OR_CREATE | Lumix::FS::Mode::WRITE);
			LUMIX_EXPECT_NOT_NULL(error_file);

			size_t size = invalid_file->size();
			invalid_file->read(buffer, size);
			error_file->write(buffer, size);

			file_system->close(invalid_file);
			file_system->close(error_file);
		}

		Lumix::g_log_info.log("unit") << "reloading invalid ...";
		animation_manager.reload(*animation);

		waitForFinishLoading(animation, file_system);

		LUMIX_EXPECT_FALSE(animation->isEmpty());
		LUMIX_EXPECT_FALSE(animation->isLoading());
		LUMIX_EXPECT_FALSE(animation->isReady());
		LUMIX_EXPECT_FALSE(animation->isUnloading());
		LUMIX_EXPECT_TRUE(animation->isFailure());

		{
			Lumix::FS::IFile* valid_file = file_system->open("memory:disk", anim_test_valid, Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);
			LUMIX_EXPECT_NOT_NULL(valid_file);

			Lumix::FS::IFile* error_file = file_system->open("memory:disk", anim_test_fail, Lumix::FS::Mode::OPEN_OR_CREATE | Lumix::FS::Mode::WRITE);
			LUMIX_EXPECT_NOT_NULL(error_file);

			size_t size = valid_file->size();
			valid_file->read(buffer, size);
			error_file->write(buffer, size);

			file_system->close(valid_file);
			file_system->close(error_file);
		}

		Lumix::g_log_info.log("unit") << "reloading valid ...";
		animation_manager.reload(*animation);

		waitForFinishLoading(animation, file_system);

		LUMIX_EXPECT_FALSE(animation->isEmpty());
		LUMIX_EXPECT_FALSE(animation->isLoading());
		LUMIX_EXPECT_TRUE(animation->isReady());
		LUMIX_EXPECT_FALSE(animation->isUnloading());
		LUMIX_EXPECT_FALSE(animation->isFailure());


		// exit
		animation_manager.releaseAll();
		animation_manager.destroy();
		resource_manager.destroy();

		file_system->unMount(&disk_file_device);
		file_system->unMount(&mem_file_device);

		Lumix::FS::FileSystem::destroy(file_system);
	}
}

REGISTER_TEST("unit_tests/engine/material_manager", UT_material_manager, "unit_tests/resource_managers/cisla.tga 262188");
REGISTER_TEST("unit_tests/engine/material_manager", UT_material_manager, "unit_tests/resource_managers/trava3.dds 2796344");
REGISTER_TEST("unit_tests/engine/animation_manager", UT_animation_manager, "unit_tests/resource_managers/blender.ani 3424");
REGISTER_TEST("unit_tests/engine/failure_reload", UT_failure_reload, "");