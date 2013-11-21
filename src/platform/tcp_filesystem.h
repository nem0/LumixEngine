#pragma once


#include "core/ifilesystem.h"


namespace Lux
{

	class LUX_PLATFORM_API TCPFileSystem : public IFileSystem
	{
		public:
			TCPFileSystem() { m_impl = 0; }

			bool create();
			void destroy();
			void processLoaded();

			virtual Handle openFile(const char* path, ReadCallback callback, void* user_data);

		private:
			struct TCPFileSystemImpl* m_impl;
	};

} // ~namespace Lux