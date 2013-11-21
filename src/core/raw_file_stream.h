#pragma once


#include "core/lux.h"
#include "core/istream.h"
#include <cstdio>


namespace Lux
{


class LUX_CORE_API RawFileStream : public IStream
{
	public:
		struct Mode
		{
			enum Value
			{
				READ,
				WRITE
			};
			Mode(Value _value) : value(_value) {}
			operator Value() { return value; }
			Value value;
		};

	public:
		RawFileStream();
		~RawFileStream();
		
		bool create(const char* path, Mode mode);
		void destroy();

		virtual void write(const void* data, size_t size) LUX_OVERRIDE;
		virtual bool read(void* data, size_t size) LUX_OVERRIDE;
	
	private:
		FILE* m_fp;
};


} // !namespace Lux