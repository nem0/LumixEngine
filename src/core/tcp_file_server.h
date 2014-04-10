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
			class IWatcher abstract
			{
				public:
					virtual void onFileOpen(const char* path, bool success) = 0;
			};

		public:
			TCPFileServer();
			~TCPFileServer();

			void start(const char* base_path);
			void stop();
			void setWatcher(IWatcher* watcher);
			const char* getBasePath() const;

		private:
			TCPFileServerImpl* m_impl;
		};
	}
}