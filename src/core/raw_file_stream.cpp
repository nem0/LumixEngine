#include "raw_file_stream.h"


namespace Lux
{


RawFileStream::RawFileStream()
{
	m_fp = 0;
}


RawFileStream::~RawFileStream()
{
	ASSERT(m_fp == 0);
}


bool RawFileStream::create(const char* path, Mode mode)
{
	const char*	mode_str = mode == Mode::READ ? "r" : "w";
	m_fp = 0;
	fopen_s(&m_fp, path, mode_str);
	return m_fp !=  0;
}


void RawFileStream::destroy()
{
	fclose(m_fp);
	m_fp = 0;
}


void RawFileStream::write(const void* data, size_t size)
{
	fwrite(data, size, 1, m_fp);
}


bool RawFileStream::read(void* data, size_t size)
{
	size_t read = fread(data, size, 1, m_fp);
	if(read == 0)
	{
		for(size_t i = 0; i < size; ++i)
			((unsigned char*)data)[i] = 0;
		return false;
	}
	return true;
}


} // !namespace Lux
