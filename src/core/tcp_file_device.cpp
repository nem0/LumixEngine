#include "core/tcp_file_device.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/ifile_system_defines.h"
#include "core/path.h"
#include "core/tcp_connector.h"
#include "core/tcp_stream.h"


namespace Lux
{
	namespace FS
	{
		class TCPFile : public IFile
		{
		public:
			TCPFile(Net::TCPStream* stream) : m_stream(stream) {}
			~TCPFile() {}

			virtual bool open(const Path& path, Mode mode) LUX_OVERRIDE
			{
				int32_t op = TCPCommand::OpenFile;
				int32_t ret = 0;

				m_stream->write(op);
				m_stream->write(mode);
				m_stream->write(path.getCString());
				m_stream->read(ret);

				return 1 == ret;
			}

			virtual void close() LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Close;
				m_stream->write(op);
			}

			virtual bool read(void* buffer, size_t size) LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Read;

				m_stream->write(op);
				m_stream->write(size);
				return m_stream->read(buffer, size);
			}

			virtual bool write(const void* buffer, size_t size) LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Write;

				m_stream->write(op);
				m_stream->write(size);
				return m_stream->write(buffer, size);
			}

			virtual const void* getBuffer() const LUX_OVERRIDE
			{
				return NULL;
			}

			virtual size_t size() LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Size;
				uint32_t size = 0;
				m_stream->write(op);
				m_stream->read(size);

				return size;
			}

			virtual size_t seek(SeekMode base, int32_t pos) LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Seek;

				m_stream->write(op);
				m_stream->write(base);
				m_stream->write(pos);

				size_t ret = 0;
				m_stream->read(ret);

				return ret;
			}

			virtual size_t pos() const LUX_OVERRIDE
			{
				int32_t op = TCPCommand::Seek;
				size_t pos = 0;

				m_stream->write(op);
				m_stream->read(pos);

				return pos;
			}

		private:
			Net::TCPStream* m_stream;
		};

		struct TCPImpl
		{
			Net::TCPConnector m_connector;
			Net::TCPStream* m_stream;
		};

		IFile* TCPFileDevice::createFile(IFile* child)
		{
			return LUX_NEW(TCPFile)(m_impl->m_stream);
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