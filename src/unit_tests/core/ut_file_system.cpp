#include "unit_tests/suite/lux_unit_tests.h"

#include "core/fs/file_system.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/file_events_device.h"
#include "core/fs/ifile.h"

namespace
{
	TODO("UT_disk_file_device");
	TODO("UT_tcp_file_device");
	TODO("UT_memory_file_device");

	uint32_t occured_event = 0;

	void fs_event_cb(const Lux::FS::Event& event)
	{
		Lux::g_log_info.log("unit", "Event: %d", (uint32_t)event.type);
		occured_event |= 1 << (uint32_t)event.type;
	}

	void UT_file_events_device(const char* params)
	{
		Lux::FS::FileSystem* file_system;
		Lux::FS::DiskFileDevice* disk_file_device;
		Lux::FS::FileEventsDevice* file_event_device;

		file_system = Lux::FS::FileSystem::create();

		disk_file_device = LUX_NEW(Lux::FS::DiskFileDevice);
		file_event_device = LUX_NEW(Lux::FS::FileEventsDevice);
		file_event_device->OnEvent.bind<fs_event_cb>();

		file_system->mount(file_event_device);
		file_system->mount(disk_file_device);

		LUX_EXPECT_FALSE(1 << (uint32_t)Lux::FS::EventType::OPEN_BEGIN & occured_event);
		LUX_EXPECT_FALSE(1 << (uint32_t)Lux::FS::EventType::OPEN_BEGIN & occured_event);

		Lux::FS::IFile* file = file_system->open("events:disk", "unit_tests/file_system/selenitic.xml", Lux::FS::Mode::OPEN | Lux::FS::Mode::READ);

		LUX_EXPECT_NOT_NULL(file);
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::OPEN_BEGIN & occured_event));
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::OPEN_BEGIN & occured_event));

		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::SIZE_BEGIN & occured_event));
		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::SIZE_FINISHED & occured_event));

		size_t size = file->size();
		LUX_EXPECT_GE(size, size_t(4));

		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::SIZE_BEGIN & occured_event));
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::SIZE_FINISHED & occured_event));

		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::SEEK_BEGIN & occured_event));
		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::SEEK_FINISHED & occured_event));

		size_t seek = file->seek(Lux::FS::SeekMode::BEGIN, size - 4);
		LUX_EXPECT_EQ(seek, size - 4);

		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::SEEK_BEGIN & occured_event));
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::SEEK_FINISHED & occured_event));

		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::POS_BEGIN & occured_event));
		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::POS_FINISHED & occured_event));

		size_t pos = file->pos();
		LUX_EXPECT_EQ(pos, size - 4);

		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::POS_BEGIN & occured_event));
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::POS_FINISHED & occured_event));

		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::READ_BEGIN & occured_event));
		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::READ_FINISHED & occured_event));

		uint32_t buff;
		bool ret = file->read(&buff, sizeof(buff));
		LUX_EXPECT_TRUE(ret);

		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::READ_BEGIN & occured_event));
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::READ_FINISHED & occured_event));

		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::CLOSE_BEGIN & occured_event));
		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::CLOSE_FINISHED & occured_event));

		file_system->close(file);

		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::CLOSE_BEGIN & occured_event));
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::CLOSE_FINISHED & occured_event));

		occured_event = 0;

		LUX_EXPECT_FALSE(1 << (uint32_t)Lux::FS::EventType::OPEN_BEGIN & occured_event);
		LUX_EXPECT_FALSE(1 << (uint32_t)Lux::FS::EventType::OPEN_BEGIN & occured_event);

		file = file_system->open("events:disk", "unit_tests/file_system/selenitic2.xml", Lux::FS::Mode::OPEN_OR_CREATE | Lux::FS::Mode::WRITE);

		LUX_EXPECT_NOT_NULL(file);

		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::OPEN_BEGIN & occured_event));
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::OPEN_BEGIN & occured_event));

		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::WRITE_BEGIN & occured_event));
		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::WRITE_FINISHED & occured_event));

		ret = file->write(&buff, sizeof(buff));
		LUX_EXPECT_TRUE(ret);

		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::WRITE_BEGIN & occured_event));
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::WRITE_FINISHED & occured_event));

		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::CLOSE_BEGIN & occured_event));
		LUX_EXPECT_FALSE(!!(1 << (uint32_t)Lux::FS::EventType::CLOSE_FINISHED & occured_event));

		file_system->close(file);

		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::CLOSE_BEGIN & occured_event));
		LUX_EXPECT_TRUE(!!(1 << (uint32_t)Lux::FS::EventType::CLOSE_FINISHED & occured_event));

		LUX_DELETE(disk_file_device);
		LUX_DELETE(file_event_device);

		Lux::FS::FileSystem::destroy(file_system);
	};
}

REGISTER_TEST("unit_tests/core/file_system/file_events_device", UT_file_events_device, "")