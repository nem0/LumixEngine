#pragma once

#include "engine/lumix.h"


#if !LUMIX_SINGLE_THREAD()


namespace Lumix
{
	class IAllocator;


	namespace FS
	{
		class LUMIX_ENGINE_API TCPFileServer
		{
		public:
			TCPFileServer();
			~TCPFileServer();

			void start(const char* base_path, IAllocator& allocator);
			void stop();
			const char* getBasePath() const;

		private:
			struct TCPFileServerImpl* m_impl;
		};
	}
}


#endif
