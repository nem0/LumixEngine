#include "core/fs/memory_file_device.h"
#include "core/fs/ifile.h"
#include "core/fs/ifile_system_defines.h"
#include "core/math_utils.h"

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
				LUX_DELETE(m_file);
				LUX_DELETE_ARRAY(m_buffer);
			}

			virtual bool open(const char* path, Mode mode) override
			{
				ASSERT(NULL == m_buffer); // reopen is not supported currently

				m_write = !!(mode & Mode::WRITE);
				if(m_file)
				{
					if(m_file->open(path, mode))
					{
						if(mode & Mode::READ)
						{
							m_capacity = m_size = m_file->size();
							m_buffer = LUX_NEW_ARRAY(uint8_t, m_size);
							m_file->read(m_buffer, m_size);
							m_pos = 0;
						}

						return true;
					}
				}
				else
				{
					if(mode & Mode::WRITE)
					{
						return true;
					}
				}

				return false;
			}

			virtual void close() override
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

				LUX_DELETE_ARRAY(m_buffer);
				m_buffer = NULL;
			}

			virtual bool read(void* buffer, size_t size) override
			{
				size_t amount = m_pos + size < m_size ? size : m_size - m_pos;
				memcpy(buffer, m_buffer + m_pos, amount);
				m_pos += amount;
				return amount == size;
			}

			virtual bool write(const void* buffer, size_t size) override
			{
				size_t pos = m_pos;
				size_t cap = m_capacity;
				size_t sz = m_size;
				if(pos + size > cap)
				{
					size_t new_cap = 0 != cap ? cap << 1 : 0x8000;
					uint8_t* new_data = LUX_NEW_ARRAY(uint8_t, new_cap);
					memcpy(new_data, m_buffer, sz);
					LUX_DELETE_ARRAY(m_buffer);
					m_buffer = new_data;
					m_capacity = new_cap;
				}

				memcpy(m_buffer + pos, buffer, size);
				m_pos += size;
				m_size = pos + size > sz ? pos + size : sz;

				return true;
			}

			virtual const void* getBuffer() const override
			{
				return m_buffer;
			}

			virtual size_t size() override
			{
				return m_size;
			}

			virtual size_t seek(SeekMode base, size_t pos) override
			{
				switch(base)
				{
				case SeekMode::BEGIN:
					ASSERT(pos >= 0 && pos <= (int32_t)m_size);
					m_pos = pos;
					break;
				case SeekMode::CURRENT:
					ASSERT(0 <= (int32_t)m_pos + pos && (int32_t)m_pos + pos <= (int32_t)m_size);
					m_pos += pos;
					break;
				case SeekMode::END:
					ASSERT(0 <= pos && pos <= (int32_t)m_size);
					m_pos = m_size - pos;
					break;
				default:
					ASSERT(0);
					break;
				}

				m_pos = Math::min(m_pos, m_size);
				return m_pos;
			}

			virtual size_t pos() override
			{
				return m_pos;
			}

		private:
			uint8_t* m_buffer;
			size_t m_size;
			size_t m_capacity;
			size_t m_pos;
			IFile* m_file;
			bool m_write;
		};

		IFile* MemoryFileDevice::createFile(IFile* child)
		{
			return LUX_NEW(MemoryFile)(child);
		}
	} // ~namespace FS
} // ~namespace Lux