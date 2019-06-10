#include "engine/array.h"
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
		struct DiskFile final : public IFile
		{
			DiskFile(DiskFileDevice& device, IAllocator& allocator)
				: m_device(device)
				, m_allocator(allocator)
				, m_data(allocator)
			{
			}


			~DiskFile()
			{
			}


			IFileDevice* getDevice() override
			{
				return &m_device;
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
				if (!m_file.open(tmp, mode)) return false;
				
				m_data.resize((int)m_file.size());
				if (!m_file.read(m_data.begin(), m_data.byte_size())) return false;

				return m_file.seek(FS::SeekMode::BEGIN, 0);
			}

			void close() override
			{
				m_file.close();
			}

			bool read(void* buffer, size_t size) override
			{
				return m_file.read(buffer, size);
			}

			bool write(const void* buffer, size_t size) override
			{
				return m_file.write(buffer, size);
			}

			const void* getBuffer() const override
			{
				return m_data.begin();
			}

			size_t size() override
			{
				return m_file.size();
			}

			bool seek(SeekMode base, size_t pos) override
			{
				return m_file.seek(base, pos);
			}

			size_t pos() override
			{
				return m_file.pos();
			}

			Array<u8> m_data;
			DiskFileDevice& m_device;
			IAllocator& m_allocator;
			OsFile m_file;
		};


		DiskFileDevice::DiskFileDevice(const char* base_path, IAllocator& allocator)
			: m_allocator(allocator)
		{
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

		IFile* DiskFileDevice::createFile()
		{
			return LUMIX_NEW(m_allocator, DiskFile)(*this, m_allocator);
		}
	} // namespace FS
} // namespace Lumix
