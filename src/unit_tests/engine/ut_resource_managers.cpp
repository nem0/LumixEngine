#include "unit_tests/suite/lux_unit_tests.h"

#include "core/file_system.h"
#include "core/memory_file_device.h"
#include "core/disk_file_device.h"

#include "core/resource_manager.h"
#include "core/resource.h"

#include "graphics/texture_manager.h"
#include "graphics/texture.h"

namespace
{
	const char texture_test_tga[] = "unit_tests/resource_managers/cisla.tga";
	const size_t texture_test_tga_size = 262188;
	const char texture_test_dds[] = "unit_tests/resource_managers/trava3.dds";
	const char texture_test_failure[] = "unit_tests/resource_managers/_non_exist.dds";

	void UT_material_manager(const char* params)
	{
		Lux::FS::FileSystem* file_system = Lux::FS::FileSystem::create();

		Lux::FS::MemoryFileDevice mem_file_device;
		Lux::FS::DiskFileDevice disk_file_device;

		file_system->mount(&mem_file_device);
		file_system->mount(&disk_file_device);
		file_system->setDefaultDevice("memory:disk");

		Lux::ResourceManager resource_manager;
		Lux::TextureManager texture_manager;
		resource_manager.create(*file_system);
		texture_manager.create(Lux::ResourceManager::TEXTURE, resource_manager);

		Lux::g_log_info.log("unit", "loading ...");
		Lux::Resource* texture_tga1 = texture_manager.load(texture_test_tga);
		Lux::Resource* texture_tga2 = texture_manager.load(texture_test_tga);
		Lux::Resource* texture_tga3 = texture_manager.get(texture_test_tga);

		LUX_EXPECT_NOT_NULL(texture_tga1);
		LUX_EXPECT_NOT_NULL(texture_tga2);
		LUX_EXPECT_NOT_NULL(texture_tga3);

		LUX_EXPECT_EQ(texture_tga1, texture_tga2);
		LUX_EXPECT_EQ(texture_tga2, texture_tga3);

		LUX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUX_EXPECT_TRUE(texture_tga1->isLoading());
		LUX_EXPECT_FALSE(texture_tga1->isReady());
		LUX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUX_EXPECT_FALSE(texture_tga1->isFailure());

		LUX_EXPECT_EQ(0, texture_tga1->size());
		
		do 
		{
			file_system->updateAsyncTransactions();
		} while (texture_tga1->isLoading());

		LUX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUX_EXPECT_FALSE(texture_tga1->isLoading());
		LUX_EXPECT_TRUE(texture_tga1->isReady());
		LUX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUX_EXPECT_FALSE(texture_tga1->isFailure());
		
		LUX_EXPECT_EQ(texture_test_tga_size, texture_tga1->size());

		Lux::g_log_info.log("unit", "unloading ...");

		texture_manager.unload(texture_test_tga);

		LUX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUX_EXPECT_FALSE(texture_tga1->isLoading());
		LUX_EXPECT_TRUE(texture_tga1->isReady());
		LUX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUX_EXPECT_FALSE(texture_tga1->isFailure());

		texture_manager.unload(*texture_tga2);

		// Should start unloading. The get method doesn't count references.
		LUX_EXPECT_TRUE(texture_tga1->isEmpty());
		LUX_EXPECT_FALSE(texture_tga1->isLoading());
		LUX_EXPECT_FALSE(texture_tga1->isReady());
		LUX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUX_EXPECT_FALSE(texture_tga1->isFailure());

		LUX_EXPECT_EQ(0, texture_tga1->size());

		Lux::g_log_info.log("unit", "loading ...");

		texture_manager.load(*texture_tga1);
		texture_manager.load(*texture_tga2);
		texture_manager.load(*texture_tga3);

		LUX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUX_EXPECT_TRUE(texture_tga1->isLoading());
		LUX_EXPECT_FALSE(texture_tga1->isReady());
		LUX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUX_EXPECT_FALSE(texture_tga1->isFailure());

		do
		{
			file_system->updateAsyncTransactions();
		} while (texture_tga1->isLoading());

		LUX_EXPECT_FALSE(texture_tga1->isEmpty());
		LUX_EXPECT_FALSE(texture_tga1->isLoading());
		LUX_EXPECT_TRUE(texture_tga1->isReady());
		LUX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUX_EXPECT_FALSE(texture_tga1->isFailure());

		LUX_EXPECT_EQ(texture_test_tga_size, texture_tga1->size());


		Lux::g_log_info.log("unit", "force unloading ...");

		texture_manager.forceUnload(texture_test_tga);

		LUX_EXPECT_TRUE(texture_tga1->isEmpty());
		LUX_EXPECT_FALSE(texture_tga1->isLoading());
		LUX_EXPECT_FALSE(texture_tga1->isReady());
		LUX_EXPECT_FALSE(texture_tga1->isUnloading());
		LUX_EXPECT_FALSE(texture_tga1->isFailure());

		LUX_EXPECT_EQ(0, texture_tga1->size());

		Lux::Resource* texture_fail = texture_manager.load(texture_test_failure);

		LUX_EXPECT_NOT_NULL(texture_fail);

		LUX_EXPECT_FALSE(texture_fail->isEmpty());
		LUX_EXPECT_TRUE(texture_fail->isLoading());
		LUX_EXPECT_FALSE(texture_fail->isReady());
		LUX_EXPECT_FALSE(texture_fail->isUnloading());
		LUX_EXPECT_FALSE(texture_fail->isFailure());

		LUX_EXPECT_EQ(0, texture_fail->size());

		do
		{
			file_system->updateAsyncTransactions();
		} while (texture_fail->isLoading());

		LUX_EXPECT_FALSE(texture_fail->isEmpty());
		LUX_EXPECT_FALSE(texture_fail->isLoading());
		LUX_EXPECT_FALSE(texture_fail->isReady());
		LUX_EXPECT_FALSE(texture_fail->isUnloading());
		LUX_EXPECT_TRUE(texture_fail->isFailure());

		// exit
		texture_manager.destroy();
		resource_manager.destroy();

		file_system->unMount(&disk_file_device);
		file_system->unMount(&mem_file_device);

		Lux::FS::FileSystem::destroy(file_system);
	}
}

TODO("string tokenizer");
REGISTER_TEST("unit_tests/engine/material_manager", UT_material_manager, "unit_tests/resource_managers/cisla.tga 262188");
REGISTER_TEST("unit_tests/engine/material_manager", UT_material_manager, "unit_tests/resource_managers/trava3.dds 2796344");