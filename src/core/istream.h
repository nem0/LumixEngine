#pragma once


#include "core/lux.h"


namespace Lux
{


class LUX_CORE_API IStream
{
	public:
		virtual void write(const void* data, size_t size) = 0;
		virtual void write(const char* data);
		virtual bool read(void* data, size_t size) = 0;
};


}