#include "core/tcp_file_system.h"
#include "core/ifile.h"
#include "core/ifile_system_defines.h"
#include "core/file_system.h"
#include "core/tcp_acceptor.h"
#include "core/tcp_stream.h"
#include "platform/task.h"


namespace Lux
{
	namespace FS
	{
		class TCPFile : public IFile
		{
		public:
			TCPFile(IFile* parent, Net::TCPStream* stream) : m_stream(stream) {}
			~TCPFile() {}

			virtual bool open(const char* path, Mode mode) LUX_OVERRIDE
			{
				while(!m_fs->isInitialized());
				int32_t op = TCPCommand::OpenFile;
				int32_t ret = 0;

				m_stream->write(op);
				m_stream->write(mode);
				m_stream->write(path);
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
			Net::TCPStream* m_stream;
		};

		class TCPFileSystemTask : public MT::Task
		{
		public:
			TCPFileSystemTask(TCPImpl* impl) : m_impl(impl) {}
			~TCPFileSystemTask() {};

			int task()
			{
				m_impl->m_stream = m_acceptor.accept(); 

				return 0;
			}

			void start(const char* ip, uint16_t port)
			{
				m_acceptor.start(ip, port);
			}

		private:
			TCPImpl* m_impl;
			Net::TCPAcceptor m_acceptor;
		};

		IFile* TCPFileSystem::create(IFile* parent)
		{
			return new TCPFile(parent, m_impl->m_stream);
		}

		void TCPFileSystem::start(const char* ip, uint16_t port)
		{
			m_impl = new TCPImpl;
			m_task = new TCPFileSystemTask(m_impl);
			m_task->start(ip, port);
			m_task->create("TCP File System");
			m_task->run();
		}

		void TCPFileSystem::stop()
		{
			// todo: destroy task after it's finished
			m_task->destroy();
			delete m_task;
			delete m_impl;
		}

		bool TCPFileSystem::isInitialized() const
		{
			return m_task->isFinished();
		}

		Net::TCPStream* TCPFileSystem::getStream()
		{
			return m_impl->m_stream;
		}
	} // namespace FS
} // ~namespace Lux