#include "core/fs/tcp_file_device.h"
#include "core/blob.h"
#include "core/fs/ifile.h"
#include "core/fs/ifile_system_defines.h"
#include "core/fs/file_system.h"
#include "core/net/tcp_connector.h"
#include "core/net/tcp_stream.h"
#include "core/MT/spin_mutex.h"


namespace Lux
{
	namespace FS
	{
		class TCPFile : public IFile
		{
		public:
			TCPFile(Net::TCPStream* stream, MT::SpinMutex& spin_mutex) 
				: m_stream(stream)
				, m_spin_mutex(spin_mutex)
			{}

			~TCPFile() {}

			virtual bool open(const char* path, Mode mode) override
			{
				int32_t op = TCPCommand::OpenFile;

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
					int32_t op = TCPCommand::Close;

					MT::SpinLock lock(m_spin_mutex);
					m_stream->write(op);
					m_stream->write(m_file);
				}
			}

			virtual bool read(void* buffer, size_t size) override
			{
				int32_t op = TCPCommand::Read;

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
				int32_t op = TCPCommand::Write;

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
				return NULL;
			}

			virtual size_t size() override
			{
				int32_t op = TCPCommand::Size;
				uint32_t size = 0;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);

				m_stream->read(size);

				return (size_t)size;
			}

			virtual size_t seek(SeekMode base, size_t pos) override
			{
				int32_t op = TCPCommand::Seek;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);
				m_stream->write(base);
				m_stream->write(pos);

				int32_t ret = 0;
				m_stream->read(ret);

				return (size_t)ret;
			}

			virtual size_t pos() override
			{
				int32_t op = TCPCommand::Seek;
				int32_t pos = 0;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);

				m_stream->read(pos);

				return (size_t)pos;
			}

		private:
			void operator=(const TCPFile&);

			Net::TCPStream* m_stream;
			MT::SpinMutex& m_spin_mutex;
			uint32_t m_file;
		};

		struct TCPImpl
		{
			TCPImpl()
				: m_spin_mutex(false)
			{}

			Net::TCPConnector m_connector;
			Net::TCPStream* m_stream;
			MT::SpinMutex m_spin_mutex;
		};

		IFile* TCPFileDevice::createFile(IFile*)
		{
			return LUX_NEW(TCPFile)(m_impl->m_stream, m_impl->m_spin_mutex);
		}

		void TCPFileDevice::connect(const char* ip, uint16_t port)
		{
			m_impl = LUX_NEW(TCPImpl);
			m_impl->m_stream = m_impl->m_connector.connect(ip, port);
		}

		void TCPFileDevice::disconnect()
		{
			m_impl->m_stream->write(TCPCommand::Disconnect);
			LUX_DELETE(m_impl->m_stream);
			LUX_DELETE(m_impl);
		}
	} // namespace FS
} // ~namespace Lux