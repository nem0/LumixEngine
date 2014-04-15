#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace FS
	{
		class LUX_CORE_API TCPFileServer
		{
		public:
			TCPFileServer();
			~TCPFileServer();

			void start(const char* base_path);
			void stop();
			const char* getBasePath() const;

		private:
			struct TCPFileServerImpl* m_impl;
		};
	}
}