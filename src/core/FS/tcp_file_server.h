#pragma once

#include "core/lumix.h"

namespace Lumix
{
	namespace FS
	{
		class LUMIX_CORE_API TCPFileServer
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