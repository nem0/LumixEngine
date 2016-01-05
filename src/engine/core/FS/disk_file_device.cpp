#include "core/fs/disk_file_device.h"
#include "core/iallocator.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/fs/ifile_system_defines.h"
#include "core/fs/os_file.h"
#include "core/path.h"


namespace Lumix
{
	namespace FS
	{
		class DiskFile : public IFile
		{
		public:
			DiskFile(DiskFileDevice& device, IAllocator& allocator) : m_device(device), m_allocator(allocator) {}

			IFileDevice& getDevice() override
			{ 
				return m_device;
			}

			bool open(const Path& path, Mode mode) override
			{
				return m_file.open(path.c_str(), mode, m_allocator);
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
				return nullptr;
			}

			size_t size() override
			{
				return m_file.size();
			}

			size_t seek(SeekMode base, size_t pos) override
			{
				return m_file.seek(base, pos);
			}

			size_t pos() override
			{
				return m_file.pos();
			}

		private:
			virtual ~DiskFile() {}

			DiskFileDevice& m_device;
			IAllocator& m_allocator;
			OsFile m_file;
		};

		void DiskFileDevice::destroyFile(IFile* file)
		{
			LUMIX_DELETE(m_allocator, file);
		}

		IFile* DiskFileDevice::createFile(IFile*)
		{
			return LUMIX_NEW(m_allocator, DiskFile)(*this, m_allocator);
		}
	} // namespace FS
} // ~namespace Lumix
