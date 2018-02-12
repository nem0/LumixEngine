#include "engine/fs/disk_file_device.h"
#include "engine/iallocator.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/string.h"


namespace Lumix
{
	namespace FS
	{
		struct DiskFile LUMIX_FINAL : public IFile
		{
			DiskFile(IFile* fallthrough, DiskFileDevice& device, IAllocator& allocator)
				: m_device(device)
				, m_allocator(allocator)
				, m_fallthrough(fallthrough)
			{
				m_use_fallthrough = false;
			}


			~DiskFile()
			{
				if (m_fallthrough) m_fallthrough->release();
			}


			IFileDevice& getDevice() override
			{
				return m_device;
			}

			bool open(const Path& path, Mode mode) override
			{
				char tmp[MAX_PATH_LENGTH];
				if (path.length() > 1 && path.c_str()[1] == ':')
				{
					copyString(tmp, path.c_str());
				}
				else
				{
					copyString(tmp, m_device.getBasePath());
					catString(tmp, path.c_str());
				}
				bool want_read = (mode & Mode::READ) != 0;
				if (want_read && !OsFile::fileExists(tmp) && m_fallthrough)
				{
					m_use_fallthrough = true;
					return m_fallthrough->open(path, mode);
				}
				return m_file.open(tmp, mode);
			}

			void close() override
			{
				if (m_fallthrough) m_fallthrough->close();
				m_file.close();
				m_use_fallthrough = false;
			}

			bool read(void* buffer, size_t size) override
			{
				if (m_use_fallthrough) return m_fallthrough->read(buffer, size);
				return m_file.read(buffer, size);
			}

			bool write(const void* buffer, size_t size) override
			{
				if (m_use_fallthrough) return m_fallthrough->write(buffer, size);
				return m_file.write(buffer, size);
			}

			const void* getBuffer() const override
			{
				if (m_use_fallthrough) return m_fallthrough->getBuffer();
				return nullptr;
			}

			size_t size() override
			{
				if (m_use_fallthrough) return m_fallthrough->size();
				return m_file.size();
			}

			bool seek(SeekMode base, size_t pos) override
			{
				if (m_use_fallthrough) return m_fallthrough->seek(base, pos);
				return m_file.seek(base, pos);
			}

			size_t pos() override
			{
				if (m_use_fallthrough) return m_fallthrough->pos();
				return m_file.pos();
			}

			DiskFileDevice& m_device;
			IAllocator& m_allocator;
			OsFile m_file;
			IFile* m_fallthrough;
			bool m_use_fallthrough;
		};


		DiskFileDevice::DiskFileDevice(const char* name, const char* base_path, IAllocator& allocator)
			: m_allocator(allocator)
		{
			copyString(m_name, name);
			PathUtils::normalize(base_path, m_base_path, lengthOf(m_base_path));
			if (m_base_path[0] != '\0') catString(m_base_path, "/");
		}

		void DiskFileDevice::setBasePath(const char* path)
		{
			PathUtils::normalize(path, m_base_path, lengthOf(m_base_path));
			if (m_base_path[0] != '\0') catString(m_base_path, "/");
		}

		void DiskFileDevice::destroyFile(IFile* file)
		{
			LUMIX_DELETE(m_allocator, file);
		}

		IFile* DiskFileDevice::createFile(IFile* fallthrough)
		{
			return LUMIX_NEW(m_allocator, DiskFile)(fallthrough, *this, m_allocator);
		}
	} // namespace FS
} // namespace Lumix
