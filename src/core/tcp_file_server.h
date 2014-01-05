#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace FS
	{
		class FileSystem;
		struct TCPFileServerImpl;
		class LUX_CORE_API TCPFileServer
		{
		public:
			TCPFileServer();
			~TCPFileServer();

			void start(FileSystem& file_system);
			void stop();

		private:
			TCPFileServerImpl* m_impl;
		};
	}
}