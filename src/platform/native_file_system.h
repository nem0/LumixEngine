#pragma once


#include "core/lux.h"
#include "core/ifilesystem.h"


namespace Lux
{

	struct NativeFileSystemImpl;


	class LUX_PLATFORM_API NativeFileSystem : public IFileSystem
	{
		public:
			bool create();

			virtual void processLoaded() LUX_OVERRIDE;
			virtual void destroy() LUX_OVERRIDE;
			virtual Handle openFile(const char* path, ReadCallback callback, void* user_data) LUX_OVERRIDE;


		private:
			NativeFileSystemImpl* m_impl;
	};


} // ~namespace Lux