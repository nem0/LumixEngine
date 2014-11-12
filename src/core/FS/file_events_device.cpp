#include "core/fs/file_events_device.h"
#include "core/iallocator.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/fs/ifile_system_defines.h"

#ifdef FILE_EVENT_DEVICE

namespace Lumix
{
	namespace FS
	{
		class EventsFile : public IFile
		{
		public:
			EventsFile(IFile& file, FileEventsDevice& device, FileEventsDevice::EventCallback& cb)
				: m_file(file)
				, m_cb(cb)
				, m_device(device)
			{
			}


			virtual ~EventsFile() 
			{
				m_file.release();
			}


			virtual IFileDevice& getDevice() override
			{
				return m_device;
			}


			virtual bool open(const char* path, Mode mode) override
			{
				invokeEvent(EventType::OPEN_BEGIN, path, -1, mode);
				bool ret = m_file.open(path, mode);
				invokeEvent(EventType::OPEN_FINISHED, path, ret ? 1 : 0, mode);

				return ret;
			}


			virtual void close() override
			{
				invokeEvent(EventType::CLOSE_BEGIN, "", -1, -1);
				m_file.close();

				invokeEvent(EventType::CLOSE_FINISHED, "", -1, -1);
			}


			virtual bool read(void* buffer, size_t size) override
			{
				invokeEvent(EventType::READ_BEGIN, "", -1, size);
				bool ret = m_file.read(buffer, size);

				invokeEvent(EventType::READ_FINISHED, "", ret ? 1 : 0, size);
				return ret;
			}


			virtual bool write(const void* buffer, size_t size) override
			{
				invokeEvent(EventType::WRITE_BEGIN, "", -1, size);
				bool ret = m_file.write(buffer, size);

				invokeEvent(EventType::WRITE_FINISHED, "", ret ? 1 : 0, size);
				return ret;
			}


			virtual const void* getBuffer() const override
			{
				return NULL;
			}


			virtual size_t size() override
			{
				invokeEvent(EventType::SIZE_BEGIN, "", -1, -1);
				size_t ret = m_file.size();

				invokeEvent(EventType::SIZE_FINISHED, "", ret, -1);
				return ret;
			}


			virtual size_t seek(SeekMode base, size_t pos) override
			{
				invokeEvent(EventType::SEEK_BEGIN, "", pos, base);
				size_t ret = m_file.seek(base, pos);

				invokeEvent(EventType::SEEK_FINISHED, "", ret, base);
				return ret;
			}


			virtual size_t pos() override
			{
				invokeEvent(EventType::POS_BEGIN, "", -1, -1);
				size_t ret = m_file.pos();

				invokeEvent(EventType::POS_FINISHED, "", ret, -1);
				return ret;
			}


		private:
			EventsFile& operator= (const EventsFile& rhs);

			void invokeEvent(EventType type, const char* path, int32_t ret, int32_t param)
			{
				Event event;
				event.type = type;
				event.handle = uintptr_t(this);
				event.path = path;
				event.ret = ret;
				event.param = param;

				m_cb.invoke(event);
			}

			FileEventsDevice& m_device;
			IFile& m_file;
			FileEventsDevice::EventCallback& m_cb;
		};


		void FileEventsDevice::destroyFile(IFile* file)
		{
			m_allocator.deleteObject(file);
		}


		IFile* FileEventsDevice::createFile(IFile* child)
		{
			return m_allocator.newObject<EventsFile>(*child, *this, OnEvent);
		}
	} // namespace FS
} // ~namespace Lumix

#endif //FILE_EVENT_DEVICE