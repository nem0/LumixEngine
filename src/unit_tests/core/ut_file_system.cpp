#include "unit_tests/suite/lumix_unit_tests.h"

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

	void fs_event_cb(const Lumix::FS::Event& event)
	{
		Lumix::g_log_info.log("unit", "Event: %d", (uint32_t)event.type);
		occured_event |= 1 << (uint32_t)event.type;
	}

	void UT_file_events_device(const char* params)
	{
		Lumix::FS::FileSystem* file_system;
		Lumix::FS::DiskFileDevice* disk_file_device;
		Lumix::FS::FileEventsDevice* file_event_device;

		file_system = Lumix::FS::FileSystem::create();

		disk_file_device = LUMIX_NEW(Lumix::FS::DiskFileDevice);
		file_event_device = LUMIX_NEW(Lumix::FS::FileEventsDevice);
		file_event_device->OnEvent.bind<fs_event_cb>();

		file_system->mount(file_event_device);
		file_system->mount(disk_file_device);

		LUMIX_EXPECT_FALSE(1 << (uint32_t)Lumix::FS::EventType::OPEN_BEGIN & occured_event);
		LUMIX_EXPECT_FALSE(1 << (uint32_t)Lumix::FS::EventType::OPEN_BEGIN & occured_event);

		Lumix::FS::IFile* file = file_system->open("events:disk", "unit_tests/file_system/selenitic.xml", Lumix::FS::Mode::OPEN | Lumix::FS::Mode::READ);

		LUMIX_EXPECT_NOT_NULL(file);
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::OPEN_BEGIN & occured_event));
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::OPEN_BEGIN & occured_event));

		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::SIZE_BEGIN & occured_event));
		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::SIZE_FINISHED & occured_event));

		size_t size = file->size();
		LUMIX_EXPECT_GE(size, size_t(4));

		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::SIZE_BEGIN & occured_event));
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::SIZE_FINISHED & occured_event));

		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::SEEK_BEGIN & occured_event));
		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::SEEK_FINISHED & occured_event));

		size_t seek = file->seek(Lumix::FS::SeekMode::BEGIN, size - 4);
		LUMIX_EXPECT_EQ(seek, size - 4);

		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::SEEK_BEGIN & occured_event));
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::SEEK_FINISHED & occured_event));

		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::POS_BEGIN & occured_event));
		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::POS_FINISHED & occured_event));

		size_t pos = file->pos();
		LUMIX_EXPECT_EQ(pos, size - 4);

		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::POS_BEGIN & occured_event));
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::POS_FINISHED & occured_event));

		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::READ_BEGIN & occured_event));
		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::READ_FINISHED & occured_event));

		uint32_t buff;
		bool ret = file->read(&buff, sizeof(buff));
		LUMIX_EXPECT_TRUE(ret);

		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::READ_BEGIN & occured_event));
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::READ_FINISHED & occured_event));

		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::CLOSE_BEGIN & occured_event));
		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::CLOSE_FINISHED & occured_event));

		file_system->close(file);

		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::CLOSE_BEGIN & occured_event));
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::CLOSE_FINISHED & occured_event));

		occured_event = 0;

		LUMIX_EXPECT_FALSE(1 << (uint32_t)Lumix::FS::EventType::OPEN_BEGIN & occured_event);
		LUMIX_EXPECT_FALSE(1 << (uint32_t)Lumix::FS::EventType::OPEN_BEGIN & occured_event);

		file = file_system->open("events:disk", "unit_tests/file_system/selenitic2.xml", Lumix::FS::Mode::OPEN_OR_CREATE | Lumix::FS::Mode::WRITE);

		LUMIX_EXPECT_NOT_NULL(file);

		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::OPEN_BEGIN & occured_event));
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::OPEN_BEGIN & occured_event));

		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::WRITE_BEGIN & occured_event));
		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::WRITE_FINISHED & occured_event));

		ret = file->write(&buff, sizeof(buff));
		LUMIX_EXPECT_TRUE(ret);

		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::WRITE_BEGIN & occured_event));
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::WRITE_FINISHED & occured_event));

		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::CLOSE_BEGIN & occured_event));
		LUMIX_EXPECT_FALSE(!!(1 << (uint32_t)Lumix::FS::EventType::CLOSE_FINISHED & occured_event));

		file_system->close(file);

		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::CLOSE_BEGIN & occured_event));
		LUMIX_EXPECT_TRUE(!!(1 << (uint32_t)Lumix::FS::EventType::CLOSE_FINISHED & occured_event));

		LUMIX_DELETE(disk_file_device);
		LUMIX_DELETE(file_event_device);

		Lumix::FS::FileSystem::destroy(file_system);
	};
}

REGISTER_TEST("unit_tests/core/file_system/file_events_device", UT_file_events_device, "")