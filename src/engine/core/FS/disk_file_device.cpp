#include "core/fs/disk_file_device.h"
#include "core/iallocator.h"
#include "core/fs/file_system.h"
#include "core/fs/os_file.h"
#include "core/path.h"
#include "core/string.h"


namespace Lumix
{
	namespace FS
	{
		class DiskFile : public IFile
		{
		public:
			DiskFile(IFile* fallthrough, DiskFileDevice& device, IAllocator& allocator)
				: m_device(device)
				, m_allocator(allocator)
				, m_fallthrough(fallthrough)
			{
				m_use_fallthrough = false;
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
				if (!OsFile::fileExists(tmp) && m_fallthrough)
				{
					m_use_fallthrough = true;
					return m_fallthrough->open(path, mode);
				}
				return m_file.open(tmp, mode, m_allocator);
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

			size_t seek(SeekMode base, size_t pos) override
			{
				if (m_use_fallthrough) return m_fallthrough->seek(base, pos);
				return m_file.seek(base, pos);
			}

			size_t pos() override
			{
				if (m_use_fallthrough) return m_fallthrough->pos();
				return m_file.pos();
			}

		private:
			virtual ~DiskFile() {}

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
			copyString(m_base_path, base_path);
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
} // ~namespace Lumix
