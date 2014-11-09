#include "core/fs/disk_file_device.h"
#include "core/allocator.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/fs/ifile_system_defines.h"
#include "core/fs/os_file.h"


namespace Lumix
{
	namespace FS
	{
		class DiskFile : public IFile
		{
		public:
			DiskFile(DiskFileDevice& device, IAllocator& allocator) : m_device(device), m_allocator(allocator) {}

			virtual IFileDevice& getDevice() override
			{ 
				return m_device;
			}

			virtual bool open(const char* path, Mode mode) override
			{
				return m_file.open(path, mode, m_allocator);
			}

			virtual void close() override
			{
				m_file.close();
			}

			virtual bool read(void* buffer, size_t size) override
			{
				return m_file.read(buffer, size);
			}

			virtual bool write(const void* buffer, size_t size) override
			{
				return m_file.write(buffer, size);
			}

			virtual const void* getBuffer() const override
			{
				return NULL;
			}

			virtual size_t size() override
			{
				return m_file.size();
			}

			virtual size_t seek(SeekMode base, size_t pos) override
			{
				return m_file.seek(base, pos);
			}

			virtual size_t pos() override
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
			m_allocator.deleteObject(file);
		}

		IFile* DiskFileDevice::createFile(IFile*)
		{
			return m_allocator.newObject<DiskFile>(*this, m_allocator);
		}
	} // namespace FS
} // ~namespace Lumix
