#pragma once


#include "core/lux.h"
#include "core/file_system.h"


namespace Lux
{

	struct NativeFileSystemImpl;


	class LUX_PLATFORM_API NativeFileSystem : public FileSystem
	{
		public:
			bool create();
			void destroy();

			virtual void open(const char* filename, OnFinished callback, void* data) LUX_OVERRIDE;

		private:
			NativeFileSystemImpl* m_impl;
	};


} // ~namespace Lux