#include "engine/core/fs/tcp_file_server.h"

#include "engine/core/array.h"
#include "engine/core/free_list.h"
#include "engine/core/fs/os_file.h"
#include "engine/core/fs/tcp_file_device.h"
#include "engine/core/mt/task.h"
#include "engine/core/path.h"
#include "engine/core/profiler.h"
#include "engine/core/string.h"
#include "engine/core/network.h"


namespace Lumix
{


namespace FS
{


class TCPFileServerTask : public MT::Task
{
public:
	explicit TCPFileServerTask(IAllocator& allocator)
		: MT::Task(allocator)
		, m_acceptor(allocator)
	{
		setMemory(m_buffer, 0, sizeof(m_buffer));
		setMemory(m_files, 0, sizeof(m_files));
	}


	~TCPFileServerTask()
	{
	}


	void openFile(Net::TCPStream* stream)
	{
		int32 mode = 0;
		stream->read(mode);
		stream->readString(m_buffer, lengthOf(m_buffer));

		int32 ret = -2;
		int32 id = m_ids.alloc();
		if (id > 0)
		{
			OsFile* file = LUMIX_NEW(getAllocator(), OsFile)();
			m_files[id] = file;

			char path[MAX_PATH_LENGTH];
			if (compareStringN(m_buffer, m_base_path.c_str(), m_base_path.length()) != 0)
			{
				copyString(path, m_base_path.c_str());
				catString(path, m_buffer);
			}
			else
			{
				copyString(path, m_buffer);
			}
			ret = file->open(path, mode, getAllocator()) ? id : -1;
			if (ret == -1)
			{
				m_ids.release(id);
				file->close();
				LUMIX_DELETE(getAllocator(), file);
			}
		}
		stream->write(ret);
	}


	void read(Net::TCPStream* stream)
	{
		bool read_successful = true;
		uint32 id = 0xffffFFFF;
		stream->read(id);
		OsFile* file = m_files[id];

		uint32 size = 0;
		stream->read(size);

		while (size > 0)
		{
			int32 read = (int32)size > lengthOf(m_buffer) ? lengthOf(m_buffer)
														   : (int32)size;
			read_successful &= file->read((void*)m_buffer, read);
			stream->write((const void*)m_buffer, read);
			size -= read;
		}

		stream->write(read_successful);
	}


	void close(Net::TCPStream* stream)
	{
		uint32 id = 0xffffFFFF;
		stream->read(id);
		OsFile* file = m_files[id];
		m_ids.release(id);

		file->close();
		LUMIX_DELETE(getAllocator(), file);
	}


	void write(Net::TCPStream* stream)
	{
		bool write_successful = true;
		uint32 id = 0xffffFFFF;
		stream->read(id);
		OsFile* file = m_files[id];

		uint32 size = 0;
		stream->read(size);

		while (size > 0)
		{
			int32 read = (int32)size > lengthOf(m_buffer) ? lengthOf(m_buffer)
														   : (int32)size;
			write_successful &= stream->read((void*)m_buffer, read);
			file->write(m_buffer, read);
			size -= read;
		}

		stream->write(write_successful);
	}


	void seek(Net::TCPStream* stream)
	{
		uint32 id = 0xffffFFFF;
		stream->read(id);
		OsFile* file = m_files[id];

		uint32 base = 0;
		int32 offset = 0;
		stream->read(base);
		stream->read(offset);

		uint32 pos = (uint32)file->seek((SeekMode)base, offset);
		stream->write(pos);
	}


	void size(Net::TCPStream* stream)
	{
		uint32 id = 0xffffFFFF;
		stream->read(id);
		OsFile* file = m_files[id];

		uint32 size = (uint32)file->size();
		stream->write(size);
	}


	void pos(Net::TCPStream* stream)
	{
		uint32 id = 0xffffFFFF;
		stream->read(id);
		OsFile* file = m_files[id];

		uint32 pos = (uint32)file->pos();
		stream->write(pos);
	}


	int task()
	{
		bool quit = false;

		m_acceptor.start("127.0.0.1", 10001);
		Net::TCPStream* stream = m_acceptor.accept();

		while (!quit)
		{
			PROFILE_BLOCK("File server operation")
			int32 op = 0;
			stream->read(op);
			switch (op)
			{
				case TCPCommand::OpenFile:
					openFile(stream);
					break;
				case TCPCommand::Close:
					close(stream);
					break;
				case TCPCommand::Read:
					read(stream);
					break;
				case TCPCommand::Write:
					write(stream);
					break;
				case TCPCommand::Size:
					size(stream);
					break;
				case TCPCommand::Seek:
					seek(stream);
					break;
				case TCPCommand::Pos:
					pos(stream);
					break;
				case TCPCommand::Disconnect:
					quit = true;
					break;
				default:
					ASSERT(0);
					break;
			}
		}

		m_acceptor.close(stream);
		return 0;
	}


	void stop() {}


	void setBasePath(const char* base_path)
	{
		int len = stringLength(base_path);
		if (len <= 0) return;

		if (base_path[len - 1] == '/')
		{
			m_base_path = base_path;
		}
		else
		{
			char tmp[MAX_PATH_LENGTH];
			copyString(tmp, base_path);
			catString(tmp, "/");
			m_base_path = tmp;
		}
	}


	const char* getBasePath() const { return m_base_path.c_str(); }

private:
	Net::TCPAcceptor m_acceptor;
	char m_buffer[0x50000];
	OsFile* m_files[0x50000];
	FreeList<int32, 0x50000> m_ids;
	Path m_base_path;
};


struct TCPFileServerImpl
{
	explicit TCPFileServerImpl(IAllocator& allocator)
		: m_task(allocator)
		, m_allocator(allocator)
	{
	}

	IAllocator& m_allocator;
	TCPFileServerTask m_task;
};


TCPFileServer::TCPFileServer()
{
	m_impl = nullptr;
}


TCPFileServer::~TCPFileServer()
{
	if (m_impl)
	{
		LUMIX_DELETE(m_impl->m_allocator, m_impl);
	}
}


void TCPFileServer::start(const char* base_path, IAllocator& allocator)
{
	m_impl = LUMIX_NEW(allocator, TCPFileServerImpl)(allocator);
	m_impl->m_task.setBasePath(base_path);
	m_impl->m_task.create("TCP File Server Task");
	m_impl->m_task.run();
}


void TCPFileServer::stop()
{
	m_impl->m_task.stop();
	m_impl->m_task.destroy();
	LUMIX_DELETE(m_impl->m_allocator, m_impl);
	m_impl = nullptr;
}

const char* TCPFileServer::getBasePath() const
{
	ASSERT(m_impl);
	return m_impl->m_task.getBasePath();
}


} // ~namespace FS


} // ~namespace Lumix
