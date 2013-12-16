#pragma once

#include "core/ifile_system_defines.h"
#include "core/lux.h"

namespace Lux
{
	namespace FS
	{
		class IFile;
		class IFileDevice;

		class LUX_CORE_API FileSystem LUX_ABSTRACT
		{
		public:
			static FileSystem* create();
			static void destroy(FileSystem* fs);

			FileSystem() {}
			virtual ~FileSystem() {}

			virtual bool mount(IFileDevice* device) = 0;
			virtual bool unMount(IFileDevice* device) = 0;

			virtual IFile* open(const char* device_list, const char* file, Mode mode) = 0;
			virtual IFile* openAsync(const char* device_list, const char* file, int mode, ReadCallback call_back, void* user_data) = 0;
			 
			virtual void close(IFile* file) = 0;
			virtual void closeAsync(IFile* file) = 0;

			virtual void updateAsyncTransactions() = 0;

			virtual const char* getDefaultDevice() const = 0;
			virtual const char* getSaveGameDevice() const = 0;
		};
	} // ~namespace FS
} // ~namespace Lux