#include "core/disk_file_device.h"

#include "core/file_system.h"
#include "core/ifile.h"
#include "core/ifile_system_defines.h"
#include "core/os_file.h"


namespace Lux
{
	namespace FS
	{
		class DiskFile : public IFile
		{
		public:
			DiskFile() {}
			virtual ~DiskFile() {}

			virtual bool open(const char* path, Mode mode) override
			{
				return m_file.open(path, mode);
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
			OsFile m_file;
		};

		IFile* DiskFileDevice::createFile(IFile*)
		{
			return LUX_NEW(DiskFile)();
		}
	} // namespace FS
} // ~namespace Lux