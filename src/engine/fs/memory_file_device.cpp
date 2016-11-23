#include "engine/fs/memory_file_device.h"
#include "engine/iallocator.h"
#include "engine/fs/file_system.h"
#include "engine/math_utils.h"
#include "engine/string.h"


namespace Lumix
{
	namespace FS
	{
		class LUMIX_ENGINE_API MemoryFile LUMIX_FINAL : public IFile
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


			IFileDevice& getDevice() override
			{
				return m_device;
			}

			bool open(const Path& path, Mode mode) override
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
							m_buffer = (u8*)m_allocator.allocate(sizeof(u8) * m_size);
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

			void close() override
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

			bool read(void* buffer, size_t size) override
			{
				size_t amount = m_pos + size < m_size ? size : m_size - m_pos;
				copyMemory(buffer, m_buffer + m_pos, (int)amount);
				m_pos += amount;
				return amount == size;
			}

			bool write(const void* buffer, size_t size) override
			{
				size_t pos = m_pos;
				size_t cap = m_capacity;
				size_t sz = m_size;
				if(pos + size > cap)
				{
					size_t new_cap = Math::maximum(cap * 2, pos + size);
					u8* new_data = (u8*)m_allocator.allocate(sizeof(u8) * new_cap);
					copyMemory(new_data, m_buffer, (int)sz);
					m_allocator.deallocate(m_buffer);
					m_buffer = new_data;
					m_capacity = new_cap;
				}

				copyMemory(m_buffer + pos, buffer, (int)size);
				m_pos += size;
				m_size = pos + size > sz ? pos + size : sz;

				return true;
			}

			const void* getBuffer() const override
			{
				return m_buffer;
			}

			size_t size() override
			{
				return m_size;
			}

			bool seek(SeekMode base, size_t pos) override
			{
				switch(base)
				{
				case SeekMode::BEGIN:
					ASSERT(pos <= (i32)m_size);
					m_pos = pos;
					break;
				case SeekMode::CURRENT:
					ASSERT(0 <= i32(m_pos + pos) && i32(m_pos + pos) <= i32(m_size));
					m_pos += pos;
					break;
				case SeekMode::END:
					ASSERT(pos <= (i32)m_size);
					m_pos = m_size - pos;
					break;
				default:
					ASSERT(0);
					break;
				}

				bool ret = m_pos <= m_size;
				m_pos = Math::minimum(m_pos, m_size);
				return ret;
			}

			size_t pos() override
			{
				return m_pos;
			}

		private:
			IAllocator& m_allocator;
			MemoryFileDevice& m_device;
			u8* m_buffer;
			size_t m_size;
			size_t m_capacity;
			size_t m_pos;
			IFile* m_file;
			bool m_write;
		};

		void MemoryFileDevice::destroyFile(IFile* file)
		{
			LUMIX_DELETE(m_allocator, file);
		}

		IFile* MemoryFileDevice::createFile(IFile* child)
		{
			return LUMIX_NEW(m_allocator, MemoryFile)(child, *this, m_allocator);
		}
	} // ~namespace FS
} // ~namespace Lumix
