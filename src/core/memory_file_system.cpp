#include "core/memory_file_system.h"
#include "core/ifile.h"
#include "core/ifile_system_defines.h"

#include <string.h>

namespace Lux
{
	namespace FS
	{
		class LUX_CORE_API MemoryFile : public IFile
		{
		public:
			MemoryFile(IFile* file)
				: m_buffer(NULL)
				, m_size(0)
				, m_capacity(0)
				, m_pos(0)
				, m_file(file) 
				, m_write(false)
			{
			}

			~MemoryFile() 
			{ 
				delete m_file; 
			}

			virtual bool open(const char* path, Mode mode) LUX_OVERRIDE
			{
				ASSERT(NULL != m_file);
				ASSERT(NULL == m_buffer); // reopen is not supported currently

				if(m_file->open(path, mode))
				{
					if(mode & Mode::WRITE)
					{
						m_capacity = m_size = 0;
						m_buffer = NULL;
						m_pos = 0;
						m_write = true;
					}
					else
					{
						m_capacity = m_size = m_file->size();
						m_buffer = new uint8_t[m_size];
						m_file->read(m_buffer, m_size);
						m_pos = 0;
					}

					return true;
				}

				return false;
			}

			virtual void close() LUX_OVERRIDE
			{
				if(m_file)
				{
					if(m_write)
					{
						m_file->seek(SeekMode::BEGIN, 0);
						m_file->write(m_buffer, m_size);
					}
					m_file->close();
				}

				delete [] m_buffer;
			}

			virtual bool read(void* buffer, size_t size) LUX_OVERRIDE
			{
				size_t amount = m_pos + size < m_size ? size : m_size - m_pos;
				memcpy(buffer, m_buffer + m_pos, amount);
				m_pos += amount;
				return amount == size;
			}

			virtual bool write(const void* buffer, size_t size) LUX_OVERRIDE
			{
				uint32_t pos = m_pos;
				uint32_t cap = m_capacity;
				uint32_t sz = m_size;
				if(pos + size > cap)
				{
					uint8_t* new_data = new uint8_t[0 != cap ? cap << 1 : 0x8000];
					memcpy(new_data, m_buffer, sz);
					delete m_buffer;
					m_buffer = new_data;
					m_capacity = cap << 1;
				}

				memcpy(m_buffer + pos, buffer, size);
				m_pos += size;
				m_size = pos + size > sz ? pos + size : sz;

				return true;
			}

			virtual const void* getBuffer() const LUX_OVERRIDE
			{
				return m_buffer;
			}

			virtual size_t size() LUX_OVERRIDE
			{
				return m_size;
			}

			virtual size_t seek(SeekMode base, int32_t pos) LUX_OVERRIDE
			{
				switch(base)
				{
				case SeekMode::BEGIN:
					m_pos = pos;
					break;
				case SeekMode::CURRENT:
					m_pos += pos;
					break;
				case SeekMode::END:
					m_pos = m_size - pos;
					break;
				default:
					ASSERT(0);
					break;
				}

				return m_pos;
			}

			virtual size_t pos() const LUX_OVERRIDE
			{
				return m_pos;
			}

		private:
			uint8_t* m_buffer;
			uint32_t m_size;
			uint32_t m_capacity;
			uint32_t m_pos;
			IFile* m_file;
			bool m_write;
		};

		IFile* MemoryFileSystem::create(IFile* child)
		{
			return new MemoryFile(child);
		}
	} // ~namespace FS
} // ~namespace Lux