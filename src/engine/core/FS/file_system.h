#pragma once

#include "core/fs/ifile_system_defines.h"
#include "lumix.h"

namespace Lumix
{

class IAllocator;
class Path;


namespace FS
{


class IFile;
class IFileDevice;


struct LUMIX_ENGINE_API DeviceList
{
	IFileDevice* m_devices[8];
};


class LUMIX_ENGINE_API FileSystem
{
public:
	static FileSystem* create(IAllocator& allocator);
	static void destroy(FileSystem* fs);

	FileSystem() {}
	virtual ~FileSystem() {}

	virtual bool mount(IFileDevice* device) = 0;
	virtual bool unMount(IFileDevice* device) = 0;

	virtual IFile* open(const DeviceList& device_list, const Path& file, Mode mode) = 0;
	virtual bool openAsync(const DeviceList& device_list,
						   const Path& file,
						   int mode,
						   const ReadCallback& call_back) = 0;

	virtual void close(IFile& file) = 0;
	virtual void closeAsync(IFile& file) = 0;

	virtual void updateAsyncTransactions() = 0;

	virtual void fillDeviceList(const char* dev, DeviceList& device_list) = 0;
	virtual const DeviceList& getDefaultDevice() const = 0;
	virtual const DeviceList& getSaveGameDevice() const = 0;
	virtual const DeviceList& getMemoryDevice() const = 0;
	virtual const DeviceList& getDiskDevice() const = 0;


	virtual void setDefaultDevice(const char* dev) = 0;
	virtual void setSaveGameDevice(const char* dev) = 0;
	virtual bool hasWork() const = 0;
};


} // ~namespace FS
} // ~namespace Lumix
