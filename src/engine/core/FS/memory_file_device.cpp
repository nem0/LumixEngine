#include "core/fs/memory_file_device.h"
#include "core/iallocator.h"
#include "core/fs/ifile.h"
#include "core/fs/ifile_system_defines.h"
#include "core/math_utils.h"


namespace Lumix
{
	namespace FS
	{
		class LUMIX_ENGINE_API MemoryFile : public IFile
		{
		public:
			MemoryFile(IFile* file, MemoryFileDevice& device, IAllocator& allocator)
				: m_device(device)
				, m_buffer(nullptr)
				, m_size(0)
				, m_capacity(0)
				, m_pos(0)
				, m_file(file) 
				, m_write(false)
				, m_allocator(allocator)
			{
			}

			~MemoryFile() 
			{ 
				if (m_file)
				{
					m_file->release();
				}
				m_allocator.deallocate(m_buffer);
			}


			virtual IFileDevice& getDevice() override
			{
				return m_device;
			}

			virtual bool open(const char* path, Mode mode) override
			{
				ASSERT(!m_buffer); // reopen is not supported currently

				m_write = !!(mode & Mode::WRITE);
				if(m_file)
				{
					if(m_file->open(path, mode))
					{
						if(mode & Mode::READ)
						{
							m_capacity = m_size = m_file->size();
							m_buffer = (uint8*)m_allocator.allocate(sizeof(uint8) * m_size);
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

				m_allocator.deallocate(m_buffer);
				m_buffer = nullptr;
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
					size_t new_cap = Math::maxValue(cap * 2, pos + size);
					uint8* new_data = (uint8*)m_allocator.allocate(sizeof(uint8) * new_cap);
					memcpy(new_data, m_buffer, sz);
					m_allocator.deallocate(m_buffer);
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
					ASSERT(pos <= (int32)m_size);
					m_pos = pos;
					break;
				case SeekMode::CURRENT:
					ASSERT(0 <= (int32)m_pos + pos && (int32)m_pos + pos <= (int32)m_size);
					m_pos += pos;
					break;
				case SeekMode::END:
					ASSERT(pos <= (int32)m_size);
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
			IAllocator& m_allocator;
			MemoryFileDevice& m_device;
			uint8* m_buffer;
			size_t m_size;
			size_t m_capacity;
			size_t m_pos;
			IFile* m_file;
			bool m_write;
		};

		void MemoryFileDevice::destroyFile(IFile* file)
		{
			m_allocator.deleteObject(file);
		}

		IFile* MemoryFileDevice::createFile(IFile* child)
		{
			return m_allocator.newObject<MemoryFile>(child, *this, m_allocator);
		}
	} // ~namespace FS
} // ~namespace Lumix
