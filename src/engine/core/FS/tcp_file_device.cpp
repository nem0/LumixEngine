#include "core/fs/tcp_file_device.h"
#include "core/iallocator.h"
#include "core/blob.h"
#include "core/fs/ifile.h"
#include "core/fs/ifile_system_defines.h"
#include "core/fs/file_system.h"
#include "core/net/tcp_connector.h"
#include "core/net/tcp_stream.h"
#include "core/mt/spin_mutex.h"


namespace Lumix
{
	namespace FS
	{
		class TCPFile : public IFile
		{
		public:
			TCPFile(Net::TCPStream* stream, TCPFileDevice& device, MT::SpinMutex& spin_mutex) 
				: m_device(device)
				, m_stream(stream)
				, m_spin_mutex(spin_mutex)
				, m_file(-1)
			{}

			~TCPFile() {}

			virtual IFileDevice& getDevice() override
			{
				return m_device;
			}

			virtual bool open(const char* path, Mode mode) override
			{
				if (!m_stream)
				{
					return false;
				}

				int32 op = TCPCommand::OpenFile;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(mode);
				m_stream->writeString(path);
				m_stream->read(m_file);

				return -1 != m_file;
			}

			virtual void close() override
			{
				if(-1 != m_file)
				{
					int32 op = TCPCommand::Close;

					MT::SpinLock lock(m_spin_mutex);
					m_stream->write(op);
					m_stream->write(m_file);
				}
			}

			virtual bool read(void* buffer, size_t size) override
			{
				int32 op = TCPCommand::Read;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);
				m_stream->write(size);

				m_stream->read(buffer, size);
				bool successful = false;
				m_stream->read(successful);

				return successful;
			}

			virtual bool write(const void* buffer, size_t size) override
			{
				int32 op = TCPCommand::Write;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);
				m_stream->write(size);
				m_stream->write(buffer, size);

				bool successful = false;
				m_stream->read(successful);

				return successful;
			}

			virtual const void* getBuffer() const override
			{
				return nullptr;
			}

			virtual size_t size() override
			{
				int32 op = TCPCommand::Size;
				uint32 size = 0;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);

				m_stream->read(size);

				return (size_t)size;
			}

			virtual size_t seek(SeekMode base, size_t pos) override
			{
				int32 op = TCPCommand::Seek;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);
				m_stream->write(base);
				m_stream->write(pos);

				int32 ret = 0;
				m_stream->read(ret);

				return (size_t)ret;
			}

			virtual size_t pos() override
			{
				int32 op = TCPCommand::Seek;
				int32 pos = 0;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);

				m_stream->read(pos);

				return (size_t)pos;
			}

		private:
			void operator=(const TCPFile&);
			TCPFile(const TCPFile&);

			TCPFileDevice& m_device;
			Net::TCPStream* m_stream;
			MT::SpinMutex& m_spin_mutex;
			uint32 m_file;
		};

		struct TCPImpl
		{
			TCPImpl(IAllocator& allocator)
				: m_spin_mutex(false)
				, m_allocator(allocator)
				, m_connector(m_allocator)
				, m_stream(nullptr)
			{}

			IAllocator& m_allocator;
			Net::TCPConnector m_connector;
			Net::TCPStream* m_stream;
			MT::SpinMutex m_spin_mutex;
		};

		IFile* TCPFileDevice::createFile(IFile*)
		{
			return LUMIX_NEW(m_impl->m_allocator, TCPFile)(m_impl->m_stream, *this, m_impl->m_spin_mutex);
		}

		void TCPFileDevice::destroyFile(IFile* file)
		{
			LUMIX_DELETE(m_impl->m_allocator, file);
		}

		void TCPFileDevice::connect(const char* ip, uint16 port, IAllocator& allocator)
		{
			m_impl = LUMIX_NEW(allocator, TCPImpl)(allocator);
			m_impl->m_stream = m_impl->m_connector.connect(ip, port);
		}

		void TCPFileDevice::disconnect()
		{
			m_impl->m_stream->write(TCPCommand::Disconnect);
			m_impl->m_connector.close(m_impl->m_stream);
			LUMIX_DELETE(m_impl->m_allocator, m_impl);
		}
	} // namespace FS
} // ~namespace Lumix
