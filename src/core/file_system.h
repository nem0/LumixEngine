#pragma once


#include "lux.h"


namespace Lux
{

	class LUX_CORE_API FileSystem
	{
		public:
			enum Status
			{
				SUCCESS,
				FAIL
			};

			typedef void (*OnFinished)(const char*, void*, void*, int, Status);

		public:
			FileSystem() {}
			virtual ~FileSystem();

			virtual void open(const char* filename, OnFinished callback, void* data) = 0 {};

		private:
			FileSystem(const FileSystem&){}
			FileSystem& operator=(const FileSystem&) { return *this; }
	};

} // ~namespace Lux