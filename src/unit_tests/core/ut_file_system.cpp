#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/fs/file_system.h"
#include "core/fs/disk_file_device.h"
#include "core/fs/file_events_device.h"
#include "core/path.h"

namespace
{


uint32 occured_event = 0;

void fs_event_cb(const Lumix::FS::Event& event)
{
	Lumix::g_log_info.log("unit") << "Event: " << (uint32)event.type;
	occured_event |= 1 << (uint32)event.type;
}

void UT_file_events_device(const char* params)
{
	Lumix::FS::FileSystem* file_system;
	Lumix::FS::DiskFileDevice* disk_file_device;
	Lumix::FS::FileEventsDevice* file_event_device;

	Lumix::DefaultAllocator allocator;
	Lumix::PathManager path_manager(allocator);
	file_system = Lumix::FS::FileSystem::create(allocator);

	disk_file_device = LUMIX_NEW(allocator, Lumix::FS::DiskFileDevice)("disk", "", allocator);
	file_event_device = LUMIX_NEW(allocator, Lumix::FS::FileEventsDevice)(allocator);
	file_event_device->OnEvent.bind<fs_event_cb>();

	file_system->mount(file_event_device);
	file_system->mount(disk_file_device);

	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::OPEN_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::OPEN_BEGIN & occured_event));

	Lumix::FS::DeviceList device_list;
	file_system->fillDeviceList("events:disk", device_list);
	Lumix::FS::IFile* file = file_system->open(
		device_list, Lumix::Path("unit_tests/file_system/selenitic.xml"), Lumix::FS::Mode::OPEN_AND_READ);

	LUMIX_EXPECT(file != nullptr);
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::OPEN_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::OPEN_BEGIN & occured_event));

	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::SIZE_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::SIZE_FINISHED & occured_event));

	size_t size = file->size();
	LUMIX_EXPECT(size >= size_t(4));

	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::SIZE_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::SIZE_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::SEEK_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::SEEK_FINISHED & occured_event));

	size_t seek = file->seek(Lumix::FS::SeekMode::BEGIN, size - 4);
	LUMIX_EXPECT(seek == size - 4);

	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::SEEK_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::SEEK_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::POS_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::POS_FINISHED & occured_event));

	size_t pos = file->pos();
	LUMIX_EXPECT(pos == size - 4);

	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::POS_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::POS_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::READ_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::READ_FINISHED & occured_event));

	uint32 buff;
	bool ret = file->read(&buff, sizeof(buff));
	LUMIX_EXPECT(ret);

	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::READ_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::READ_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::CLOSE_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::CLOSE_FINISHED & occured_event));

	file_system->close(*file);

	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::CLOSE_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::CLOSE_FINISHED & occured_event));

	occured_event = 0;

	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::OPEN_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::OPEN_BEGIN & occured_event));

	file = file_system->open(device_list,
		Lumix::Path("unit_tests/file_system/selenitic2.xml"),
		Lumix::FS::Mode::CREATE_AND_WRITE);

	LUMIX_EXPECT(file != nullptr);

	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::OPEN_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::OPEN_BEGIN & occured_event));

	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::WRITE_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::WRITE_FINISHED & occured_event));

	ret = file->write(&buff, sizeof(buff));
	LUMIX_EXPECT(ret);

	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::WRITE_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::WRITE_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::CLOSE_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (uint32)Lumix::FS::EventType::CLOSE_FINISHED & occured_event));

	file_system->close(*file);

	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::CLOSE_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (uint32)Lumix::FS::EventType::CLOSE_FINISHED & occured_event));

	LUMIX_DELETE(allocator, disk_file_device);
	LUMIX_DELETE(allocator, file_event_device);

	Lumix::FS::FileSystem::destroy(file_system);
};


} // anonymous namespace

REGISTER_TEST("unit_tests/core/file_system/file_events_device", UT_file_events_device, "")