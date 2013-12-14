#include "core/disk_file_system.h"

#include "core/file_system.h"
#include "core/ifile.h"
#include "core/ifile_system_defines.h"
#include "platform/os_file.h"


namespace Lux
{
	namespace FS
	{
		class DiskFile : public IFile
		{
		public:
			DiskFile() {}
			virtual ~DiskFile() {}

			virtual bool open(const char* path, Mode mode) LUX_OVERRIDE
			{
				return m_file.open(path, mode);
			}

			virtual void close() LUX_OVERRIDE
			{
				m_file.close();
			}

			virtual bool read(void* buffer, size_t size) LUX_OVERRIDE
			{
				return m_file.read(buffer, size);
			}

			virtual bool write(const void* buffer, size_t size) LUX_OVERRIDE
			{
				return m_file.write(buffer, size);
			}

			virtual const void* getBuffer() const LUX_OVERRIDE
			{
				return NULL;
			}

			virtual size_t size() LUX_OVERRIDE
			{
				return m_file.size();
			}

			virtual size_t seek(SeekMode base, int32_t pos) LUX_OVERRIDE
			{
				return m_file.seek(base, pos);
			}

			virtual size_t pos() const LUX_OVERRIDE
			{
				return m_file.pos();
			}

		private:
			OsFile m_file;
		};

		IFile* DiskFileSystem::create(IFile* child)
		{
			return new DiskFile();
		}
	} // namespace FS
} // ~namespace Lux