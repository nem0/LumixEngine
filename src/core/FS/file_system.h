#pragma once

#include "core/fs/ifile_system_defines.h"
#include "core/lumix.h"

namespace Lumix
{
	class IAllocator;

	namespace FS
	{
		class IFile;
		class IFileDevice;

		class LUMIX_CORE_API FileSystem abstract
		{
		public:
			static FileSystem* create(IAllocator& allocator);
			static void destroy(FileSystem* fs);

			FileSystem() {}
			virtual ~FileSystem() {}

			virtual bool mount(IFileDevice* device) = 0;
			virtual bool unMount(IFileDevice* device) = 0;

			virtual IFile* open(const char* device_list, const char* file, Mode mode) = 0;
			virtual bool openAsync(const char* device_list, const char* file, int mode, const ReadCallback& call_back) = 0;
			 
			virtual void close(IFile* file) = 0;
			virtual void closeAsync(IFile* file) = 0;

			virtual void updateAsyncTransactions() = 0;

			virtual const char* getDefaultDevice() const = 0;
			virtual const char* getSaveGameDevice() const = 0;

			virtual void setDefaultDevice(const char* dev) = 0;
			virtual void setSaveGameDevice(const char* dev) = 0;
		};
	} // ~namespace FS
} // ~namespace Lumix
