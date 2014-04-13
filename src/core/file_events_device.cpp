#include "core/file_events_device.h"

#include "core/file_system.h"
#include "core/ifile.h"
#include "core/ifile_system_defines.h"

#ifdef FILE_EVENT_DEVICE

namespace Lux
{
	namespace FS
	{
		class EventsFile : public IFile
		{
		public:
			EventsFile(IFile& file, EventCallback& cb)
				: m_file(file)
				, m_cb(cb)
			{
			}

			virtual ~EventsFile() 
			{
			}

			virtual bool open(const char* path, Mode mode) override
			{
				Event event;
				event.type = EventType::OPEN_BEGIN;
				event.handle = uintptr_t(this);
				event.path = path;
				event.ret = -1;
				event.param = -1;

				m_cb.invoke(event);
				bool ret = m_file.open(path, mode);

				event.type = EventType::OPEN_FINISHED;
				event.ret = ret ? 1 : 0;
				event.param = -1;
				m_cb.invoke(event);
				return ret;
			}

			virtual void close() override
			{
				Event event;
				event.type = EventType::CLOSE_BEGIN;
				event.handle = uintptr_t(this);
				event.path = "";
				event.ret = -1;
				event.param = -1;

				m_cb.invoke(event);
				m_file.close();

				event.type = EventType::CLOSE_FINISHED;
				m_cb.invoke(event);
			}

			virtual bool read(void* buffer, size_t size) override
			{
				Event event;
				event.type = EventType::READ_BEGIN;
				event.handle = uintptr_t(this);
				event.path = "";
				event.ret = -1;
				event.param = size;

				m_cb.invoke(event);
				bool ret = m_file.read(buffer, size);

				event.type = EventType::READ_FINISHED;
				event.ret = ret ? 1 : 0;

				m_cb.invoke(event);
				return ret;
			}

			virtual bool write(const void* buffer, size_t size) override
			{
				Event event;
				event.type = EventType::WRITE_BEGIN;
				event.handle = uintptr_t(this);
				event.path = "";
				event.ret = -1;
				event.param = size;

				m_cb.invoke(event);
				bool ret = m_file.write(buffer, size);

				event.type = EventType::WRITE_FINISHED;
				event.ret = ret ? 1 : 0;

				m_cb.invoke(event);
				return ret;
			}

			virtual const void* getBuffer() const override
			{
				return NULL;
			}

			virtual size_t size() override
			{
				Event event;
				event.type = EventType::SIZE_BEGIN;
				event.handle = uintptr_t(this);
				event.path = "";
				event.ret = -1;
				event.param = -1;

				m_cb.invoke(event);
				size_t ret = m_file.size();

				event.type = EventType::SIZE_FINISHED;
				event.ret = ret;

				m_cb.invoke(event);
				return ret;
			}

			virtual size_t seek(SeekMode base, size_t pos) override
			{
				Event event;
				event.type = EventType::SEEK_BEGIN;
				event.handle = uintptr_t(this);
				event.path = "";
				event.ret = pos;
				event.param = base;

				m_cb.invoke(event);
				size_t ret = m_file.seek(base, pos);


				event.type = EventType::SEEK_FINISHED;
				event.ret = ret;

				m_cb.invoke(event);
				return ret;
			}

			virtual size_t pos() override
			{
				Event event;
				event.type = EventType::POS_BEGIN;
				event.handle = uintptr_t(this);
				event.path = "";
				event.ret = -1;
				event.param = -1;

				m_cb.invoke(event);
				size_t ret = m_file.pos();

				event.type = EventType::POS_FINISHED;
				event.ret = ret;

				m_cb.invoke(event);
				return ret;
			}

		private:
			IFile& m_file;
			EventCallback& m_cb;
		};


		IFile* FileEventsDevice::createFile(IFile* child)
		{
			return LUX_NEW(EventsFile)(*child, OnEvent);
		}
	} // namespace FS
} // ~namespace Lux

#endif //FILE_EVENT_DEVICE