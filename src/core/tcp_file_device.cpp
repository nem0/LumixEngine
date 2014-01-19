#include "core/tcp_file_device.h"
#include "core/blob.h"
#include "core/ifile.h"
#include "core/ifile_system_defines.h"
#include "core/file_system.h"
#include "core/tcp_connector.h"
#include "core/tcp_stream.h"
#include "core/spin_mutex.h"


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

			virtual bool open(const char* path, Mode mode) LUX_OVERRIDE
			{
				int32_t op = TCPCommand::OpenFile;
				int32_t ret = 0;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(mode);
				m_stream->write(path);
				m_stream->read(m_file);

				return -1 != m_file;
			}

			virtual void close() LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Close;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);
			}

			virtual bool read(void* buffer, size_t size) LUX_OVERRIDE
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

			virtual bool write(const void* buffer, size_t size) LUX_OVERRIDE
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

			virtual const void* getBuffer() const LUX_OVERRIDE
			{
				return NULL;
			}

			virtual size_t size() LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Size;
				uint32_t size = 0;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);

				m_stream->read(size);

				return size;
			}

			virtual size_t seek(SeekMode base, int32_t pos) LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Seek;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);
				m_stream->write(base);
				m_stream->write(pos);

				size_t ret = 0;
				m_stream->read(ret);

				return ret;
			}

			virtual size_t pos() LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Seek;
				size_t pos = 0;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);

				m_stream->read(pos);

				return pos;
			}

		private:
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

		IFile* TCPFileDevice::createFile(IFile* child)
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
			LUX_DELETE(m_impl);
		}
	} // namespace FS
} // ~namespace Lux