#pragma once

#include "engine/lumix.h"
#include "engine/core/delegate.h"

namespace Lumix
{

class IAllocator;
class Path;


namespace FS
{


class IFileDevice;


struct Mode
{
	enum Value
	{
		NONE = 0,
		READ = 0x1,
		WRITE = READ << 1,
		OPEN = WRITE << 1,
		CREATE = OPEN << 1,

		CREATE_AND_WRITE = CREATE | WRITE,
		OPEN_AND_READ = OPEN | READ
	};

	Mode() : value(0) {}
	Mode(Value _value) : value(_value) { }
	Mode(int32 _value) : value(_value) { }
	operator Value() const { return (Value)value; }
	int32 value;
};


struct SeekMode
{
	enum Value
	{
		BEGIN = 0,
		END,
		CURRENT,
	};
	SeekMode(Value _value) : value(_value) {}
	SeekMode(uint32 _value) : value(_value) {}
	operator Value() { return (Value)value; }
	uint32 value;
};


class LUMIX_ENGINE_API IFile
{
public:
	IFile() {}
	virtual ~IFile() {}

	virtual bool open(const Path& path, Mode mode) = 0;
	virtual void close() = 0;

	virtual bool read(void* buffer, size_t size) = 0;
	virtual bool write(const void* buffer, size_t size) = 0;

	virtual const void* getBuffer() const = 0;
	virtual size_t size() = 0;

	virtual bool seek(SeekMode base, size_t pos) = 0;
	virtual size_t pos() = 0;

	IFile& operator << (const char* text);

	void release();

protected:
	virtual IFileDevice& getDevice() = 0;

};


typedef Delegate<void(IFile&, bool)> ReadCallback;


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
