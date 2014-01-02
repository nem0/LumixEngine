#include "core/tcp_file_server.h"

#include "core/array.h"
#include "core/free_list.h"
#include "core/tcp_file_device.h"
#include "platform/task.h"
#include "platform/tcp_acceptor.h"
#include "platform/tcp_stream.h"
#include "platform/os_file.h"

namespace Lux
{
	namespace FS
	{
		class TCPFileServerTask : public MT::Task
		{
		public:
			TCPFileServerTask() {}
			~TCPFileServerTask() {}

			int task()
			{
				Array<char, 1024> buffer;
				bool quit = false;

				m_acceptor.start("127.0.0.1", 10001);
				Net::TCPStream* stream = m_acceptor.accept();

				Array<OsFile*, 0x8000> files;
				FreeList<int32_t, 0x8000> ids;

				while(!quit)
				{
					int32_t op = 0;
					stream->read(op);
					switch(op)
					{
					case TCPCommand::OpenFile:
						{
							int32_t mode = 0;
							int32_t len = 0;
							stream->read(mode);
							stream->read(buffer.data(), buffer.size());

							int32_t ret = -2;
							int32_t id = ids.alloc();
							if(id > 0)
							{
								OsFile* file = new OsFile();
								files[id] = file;

								ret = file->open(buffer.data(), mode) ? id : -1;
							}
							stream->write(ret);
						}
						break;
					case TCPCommand::Close:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = files[id];
							ids.release(id);

							file->close();
							delete file;
						}
						break;
					case TCPCommand::Read:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = files[id];

							uint32_t size = 0;
							stream->read(size);

							while(size > 0)
							{
								int32_t read = size > buffer.size() ? buffer.size() : size;
								file->read(buffer.data(), read);
								stream->write(buffer.data(), read);
								size -= read;
							}
						}
						break;
					case TCPCommand::Write:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = files[id];

							uint32_t size = 0;
							stream->read(size);

							while(size > 0)
							{
								int32_t read = size > buffer.size() ? buffer.size() : size;
								stream->read(buffer.data(), read);
								file->write(buffer.data(), read);
								size -= read;
							}
						}
						break;
					case TCPCommand::Size:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = files[id];

							uint32_t size = file->size();
							stream->write(size);
						}
						break;
					case TCPCommand::Seek:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = files[id];

							uint32_t base = 0;
							int32_t offset = 0;
							stream->read(base);
							stream->read(offset);
							
							uint32_t pos = file->seek((SeekMode)base, offset);
							stream->write(pos);
						}
						break;
					case TCPCommand::Pos:
						{
							uint32_t id = -1;
							stream->read(id);
							OsFile* file = files[id];

							uint32_t pos = file->pos();
							stream->write(pos);
						}
						break;
					case TCPCommand::Disconnect:
						{
							quit = true;
							break;
						}
					default:
						ASSERT(0);
						break;
					}
				}
				return 0;
			}

			void stop() {} // TODO: implement stop 

		private:
			Net::TCPAcceptor m_acceptor;
		};

		struct TCPFileServerImpl
		{
			TCPFileServerTask m_task;
		};

		TCPFileServer::TCPFileServer()
		{
			m_impl = NULL;
		}

		TCPFileServer::~TCPFileServer()
		{
			delete m_impl;
		}

		void TCPFileServer::start()
		{
			m_impl = new TCPFileServerImpl;
			m_impl->m_task.create("TCP File Server Task");
			m_impl->m_task.run();
		}

		void TCPFileServer::stop()
		{
			m_impl->m_task.stop();
			m_impl->m_task.destroy();
			delete m_impl;
		}
	} // ~namespace FS
} // ~namespace Lux