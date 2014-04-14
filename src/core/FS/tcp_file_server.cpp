#include "core/fs/tcp_file_server.h"

#include "core/array.h"
#include "core/free_list.h"
#include "core/os_file.h"
#include "core/path.h"
#include "core/static_array.h"
#include "core/string.h"
#include "core/fs/tcp_file_device.h"
#include "core/task.h"
#include "core/tcp_acceptor.h"
#include "core/tcp_stream.h"

namespace Lux
{
	namespace FS
	{
		class TCPFileServerTask : public MT::Task
		{
		public:
			TCPFileServerTask() 
			{}


			~TCPFileServerTask() 
			{}


			int task()
			{
				bool quit = false;

				m_acceptor.start("127.0.0.1", 10001);
				Net::TCPStream* stream = m_acceptor.accept();

				while(!quit)
				{
					int32_t op = 0;
					stream->read(op);
					switch(op)
					{
					case TCPCommand::OpenFile:
						{
							int32_t mode = 0;
							stream->read(mode);
							stream->readString(m_buffer.data(), m_buffer.size());

							int32_t ret = -2;
							int32_t id = m_ids.alloc();
							if(id > 0)
							{
								OsFile* file = LUX_NEW(OsFile)();
								m_files[id] = file;

								string path;
								if (strncmp(m_buffer.data(), m_base_path.c_str(), m_base_path.length()) != 0)
								{
									path = m_base_path.c_str();
									path += m_buffer.data();
								}
								else
								{
									path = m_buffer.data();
								}
								ret = file->open(path.c_str(), mode) ? id : -1;
							}
							stream->write(ret);
						}
						break;
					case TCPCommand::Close:
						{
							uint32_t id = 0xffffFFFF;
							stream->read(id);
							OsFile* file = m_files[id];
							m_ids.release(id);

							file->close();
							LUX_DELETE(file);
						}
						break;
					case TCPCommand::Read:
						{
							bool read_successful = true;
							uint32_t id = 0xffffFFFF;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t size = 0;
							stream->read(size);

							while(size > 0)
							{
								int32_t read = (int32_t)size > m_buffer.size() ? m_buffer.size() : (int32_t)size;
								read_successful &= file->read((void*)m_buffer.data(), read);
								stream->write((const void*)m_buffer.data(), read);
								size -= read;
							}

							stream->write(read_successful);
						}
						break;
					case TCPCommand::Write:
						{
							bool write_successful = true;
							uint32_t id = 0xffffFFFF;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t size = 0;
							stream->read(size);
							
							while(size > 0)
							{
								int32_t read = (int32_t)size > m_buffer.size() ? m_buffer.size() : (int32_t)size;
								write_successful &= stream->read((void*)m_buffer.data(), read);
								file->write(m_buffer.data(), read);
								size -= read;
							}

							stream->write(write_successful);
						}
						break;
					case TCPCommand::Size:
						{
							uint32_t id = 0xffffFFFF;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t size = (uint32_t)file->size();
							stream->write(size);
						}
						break;
					case TCPCommand::Seek:
						{
							uint32_t id = 0xffffFFFF;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t base = 0;
							int32_t offset = 0;
							stream->read(base);
							stream->read(offset);
							
							uint32_t pos = (uint32_t)file->seek((SeekMode)base, offset);
							stream->write(pos);
						}
						break;
					case TCPCommand::Pos:
						{
							uint32_t id = 0xffffFFFF;
							stream->read(id);
							OsFile* file = m_files[id];

							uint32_t pos = (uint32_t)file->pos();
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

				LUX_DELETE(stream);
				return 0;
			}


			void stop() {} // TODO: implement stop 


			void setBasePath(const char* base_path) 
			{
				string base_path_str(base_path);
				if (base_path_str[base_path_str.length() - 1] != '/')
				{
					base_path_str += "/";
				}
				m_base_path = base_path_str;
			}


			const char* getBasePath() const
			{
				return m_base_path.c_str();
			}

		private:
			Net::TCPAcceptor			m_acceptor;
			StaticArray<char, 0x50000>	m_buffer;
			StaticArray<OsFile*, 0x50000> m_files;
			FreeList<int32_t, 0x50000>	m_ids;
			Path m_base_path;
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
			LUX_DELETE(m_impl);
		}


		void TCPFileServer::start(const char* base_path)
		{
			m_impl = LUX_NEW(TCPFileServerImpl);
			m_impl->m_task.setBasePath(base_path);
			m_impl->m_task.create("TCP File Server Task");
			m_impl->m_task.run();
		}


		void TCPFileServer::stop()
		{
			m_impl->m_task.stop();
			m_impl->m_task.destroy();
			LUX_DELETE(m_impl);
			m_impl = NULL;
		}

		const char* TCPFileServer::getBasePath() const
		{
			ASSERT(m_impl);
			return m_impl->m_task.getBasePath();
		}


	} // ~namespace FS
} // ~namespace Lux