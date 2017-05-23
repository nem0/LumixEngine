#include "unit_tests/suite/lumix_unit_tests.h"

#include "engine/fs/file_system.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/file_events_device.h"
#include "engine/path.h"


using namespace Lumix;


namespace
{


u32 occured_event = 0;

void fs_event_cb(const FS::Event& event)
{
	g_log_info.log("unit") << "Event: " << (u32)event.type;
	occured_event |= 1 << (u32)event.type;
}

void UT_file_events_device(const char* params)
{
	FS::FileSystem* file_system;
	FS::DiskFileDevice* disk_file_device;
	FS::FileEventsDevice* file_event_device;

	DefaultAllocator allocator;
	PathManager path_manager(allocator);
	file_system = FS::FileSystem::create(allocator);

	disk_file_device = LUMIX_NEW(allocator, FS::DiskFileDevice)("disk", "", allocator);
	file_event_device = LUMIX_NEW(allocator, FS::FileEventsDevice)(allocator);
	file_event_device->OnEvent.bind<fs_event_cb>();

	file_system->mount(file_event_device);
	file_system->mount(disk_file_device);

	LUMIX_EXPECT(!(1 << (u32)FS::EventType::OPEN_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (u32)FS::EventType::OPEN_BEGIN & occured_event));

	FS::DeviceList device_list;
	file_system->fillDeviceList("events:disk", device_list);
	FS::IFile* file = file_system->open(
		device_list, Path("unit_tests/file_system/selenitic.xml"), FS::Mode::OPEN_AND_READ);

	LUMIX_EXPECT(file != nullptr);
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::OPEN_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::OPEN_BEGIN & occured_event));

	LUMIX_EXPECT(!(1 << (u32)FS::EventType::SIZE_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (u32)FS::EventType::SIZE_FINISHED & occured_event));

	size_t size = file->size();
	LUMIX_EXPECT(size >= size_t(4));

	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::SIZE_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::SIZE_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (u32)FS::EventType::SEEK_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (u32)FS::EventType::SEEK_FINISHED & occured_event));

	bool seek_res = file->seek(FS::SeekMode::BEGIN, size - 4);
	LUMIX_EXPECT(seek_res);

	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::SEEK_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::SEEK_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (u32)FS::EventType::POS_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (u32)FS::EventType::POS_FINISHED & occured_event));

	size_t pos = file->pos();
	LUMIX_EXPECT(pos == size - 4);

	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::POS_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::POS_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (u32)FS::EventType::READ_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (u32)FS::EventType::READ_FINISHED & occured_event));

	u32 buff;
	bool ret = file->read(&buff, sizeof(buff));
	LUMIX_EXPECT(ret);

	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::READ_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::READ_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (u32)FS::EventType::CLOSE_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (u32)FS::EventType::CLOSE_FINISHED & occured_event));

	file_system->close(*file);

	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::CLOSE_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::CLOSE_FINISHED & occured_event));

	occured_event = 0;

	LUMIX_EXPECT(!(1 << (u32)FS::EventType::OPEN_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (u32)FS::EventType::OPEN_BEGIN & occured_event));

	file = file_system->open(device_list,
		Path("unit_tests/file_system/selenitic2.xml"),
		FS::Mode::CREATE_AND_WRITE);

	LUMIX_EXPECT(file != nullptr);

	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::OPEN_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::OPEN_BEGIN & occured_event));

	LUMIX_EXPECT(!(1 << (u32)FS::EventType::WRITE_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (u32)FS::EventType::WRITE_FINISHED & occured_event));

	ret = file->write(&buff, sizeof(buff));
	LUMIX_EXPECT(ret);

	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::WRITE_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::WRITE_FINISHED & occured_event));

	LUMIX_EXPECT(!(1 << (u32)FS::EventType::CLOSE_BEGIN & occured_event));
	LUMIX_EXPECT(!(1 << (u32)FS::EventType::CLOSE_FINISHED & occured_event));

	file_system->close(*file);

	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::CLOSE_BEGIN & occured_event));
	LUMIX_EXPECT(!!(1 << (u32)FS::EventType::CLOSE_FINISHED & occured_event));

	LUMIX_DELETE(allocator, disk_file_device);
	LUMIX_DELETE(allocator, file_event_device);

	FS::FileSystem::destroy(file_system);
};


} // anonymous namespace

REGISTER_TEST("unit_tests/engine/file_system/file_events_device", UT_file_events_device, "")
