#pragma once


#include "core/lux.h"


namespace Lux
{
	
	class LUX_CORE_API IFileSystem
	{
		public:
			typedef int Handle;
			typedef void (*ReadCallback)(void* user_data, char* file_data, int length, bool success);

		public:
			virtual ~IFileSystem();

			virtual void processLoaded() = 0;
			virtual void destroy() {}
			virtual Handle openFile(const char* path, ReadCallback callback, void* user_data) = 0;
	};

} // ~namespace Lux