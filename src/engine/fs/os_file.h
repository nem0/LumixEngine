#pragma once

#include "engine/fs/file_system.h"
#include "engine/lumix.h"

namespace Lumix
{
	struct IAllocator;

	namespace FS
	{
		class LUMIX_ENGINE_API OsFile
		{
		public:
			OsFile();
			~OsFile();

			bool open(const char* path, Mode mode);
			void close();
			void flush();

			bool write(const void* data, size_t size);
			bool writeText(const char* text);
			bool read(void* data, size_t size);

			size_t size();
			size_t pos();

			bool seek(SeekMode base, size_t pos);

			OsFile& operator <<(const char* text);
			OsFile& operator <<(char c) { write(&c, sizeof(c)); return *this; }
			OsFile& operator <<(i32 value);
			OsFile& operator <<(u32 value);
			OsFile& operator <<(u64 value);
			OsFile& operator <<(float value);

			static bool fileExists(const char* path);

		private:
			void* m_handle;
		};

	
		struct OSFileStream : FS::IFile 
		{
			bool open(const Path& path, FS::Mode mode) override;
			void close() override;

			bool read(void* buffer, size_t size) override;
			bool write(const void* buffer, size_t size) override;

			const void* getBuffer() const override;
			size_t size() override;

			bool seek(FS::SeekMode base, size_t pos) override;
			size_t pos() override;
	
			FS::IFileDevice* getDevice() override;
	
			FS::OsFile file;
		};

	} // namespace FS

} // namespace Lumix
