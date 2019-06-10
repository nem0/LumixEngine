#pragma once

#include "engine/lumix.h"
#include "engine/stream.h"

namespace Lumix
{

struct IAllocator;

namespace FS
{
	
class LUMIX_ENGINE_API OSInputFile final : public IInputStream
{
public:
	OSInputFile();
	~OSInputFile();

	bool open(const char* path);
	void close();

	bool read(void* data, u64 size) override;
	const void* getBuffer() const override { return nullptr; }

	u64 size() const override;
	u64 pos();

	bool seek(size_t pos);
	
private:
	void* m_handle;
};
	

class LUMIX_ENGINE_API OSOutputFile final : public IOutputStream
{
public:
	OSOutputFile();
	~OSOutputFile();

	bool open(const char* path);
	void close();
	void flush();

	bool write(const void* data, u64 size) override;

	size_t pos();

	OSOutputFile& operator <<(const char* text);
	OSOutputFile& operator <<(char c) { write(&c, sizeof(c)); return *this; }
	OSOutputFile& operator <<(i32 value);
	OSOutputFile& operator <<(u32 value);
	OSOutputFile& operator <<(u64 value);
	OSOutputFile& operator <<(float value);

private:
	void* m_handle;
};
	
} // namespace FS

} // namespace Lumix
