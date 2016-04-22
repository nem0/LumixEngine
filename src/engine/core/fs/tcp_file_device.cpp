#include "engine/core/fs/tcp_file_device.h"
#include "engine/core/iallocator.h"
#include "engine/core/blob.h"
#include "engine/core/fs/file_system.h"
#include "engine/core/mt/sync.h"
#include "engine/core/path.h"
#include "engine/core/network.h"


namespace Lumix
{
	namespace FS
	{
		static const uint32 INVALID_FILE = 0xffffFFFF;

		class TCPFile : public IFile
		{
		public:
			TCPFile(Net::TCPStream* stream, TCPFileDevice& device, MT::SpinMutex& spin_mutex) 
				: m_device(device)
				, m_stream(stream)
				, m_spin_mutex(spin_mutex)
				, m_file(INVALID_FILE)
			{}

			~TCPFile() {}

			IFileDevice& getDevice() override
			{
				return m_device;
			}

			bool open(const Path& path, Mode mode) override
			{
				if (!m_stream)
				{
					return false;
				}

				int32 op = TCPCommand::OpenFile;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(mode);
				m_stream->writeString(path.c_str());
				m_stream->read(m_file);

				return INVALID_FILE != m_file;
			}

			void close() override
			{
				if (INVALID_FILE != m_file)
				{
					int32 op = TCPCommand::Close;

					MT::SpinLock lock(m_spin_mutex);
					m_stream->write(op);
					m_stream->write(m_file);
				}
			}

			bool read(void* buffer, size_t size) override
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

			bool write(const void* buffer, size_t size) override
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

			const void* getBuffer() const override
			{
				return nullptr;
			}

			size_t size() override
			{
				int32 op = TCPCommand::Size;
				uint32 size = 0;

				MT::SpinLock lock(m_spin_mutex);
				m_stream->write(op);
				m_stream->write(m_file);

				m_stream->read(size);

				return (size_t)size;
			}

			size_t seek(SeekMode base, size_t pos) override
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

			size_t pos() override
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
			explicit TCPImpl(IAllocator& allocator)
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

		TCPFileDevice::TCPFileDevice()
			: m_impl(nullptr)
		{
		}

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
