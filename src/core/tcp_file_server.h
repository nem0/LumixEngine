#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace FS
	{
		struct TCPFileServerImpl;
		class LUX_CORE_API TCPFileServer
		{
		public:
			TCPFileServer();
			~TCPFileServer();

			void start();
			void stop();

		private:
			TCPFileServerImpl* m_impl;
		};
	}
}