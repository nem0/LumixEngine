#include "engine/core/fs/file_events_device.h"
#include "engine/core/iallocator.h"
#include "engine/core/fs/file_system.h"
#include "engine/core/path.h"

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


			IFileDevice& getDevice() override
			{
				return m_device;
			}


			bool open(const Path& path, Mode mode) override
			{
				invokeEvent(EventType::OPEN_BEGIN, path.c_str(), -1, mode);
				bool ret = m_file.open(path, mode);
				invokeEvent(EventType::OPEN_FINISHED, path.c_str(), ret ? 1 : 0, mode);

				return ret;
			}


			void close() override
			{
				invokeEvent(EventType::CLOSE_BEGIN, "", -1, -1);
				m_file.close();

				invokeEvent(EventType::CLOSE_FINISHED, "", -1, -1);
			}


			bool read(void* buffer, size_t size) override
			{
				invokeEvent(EventType::READ_BEGIN, "", -1, (int32)size);
				bool ret = m_file.read(buffer, size);

				invokeEvent(EventType::READ_FINISHED, "", ret ? 1 : 0, (int32)size);
				return ret;
			}


			bool write(const void* buffer, size_t size) override
			{
				invokeEvent(EventType::WRITE_BEGIN, "", -1, (int32)size);
				bool ret = m_file.write(buffer, size);

				invokeEvent(EventType::WRITE_FINISHED, "", ret ? 1 : 0, (int32)size);
				return ret;
			}


			const void* getBuffer() const override
			{
				return m_file.getBuffer();
			}


			size_t size() override
			{
				invokeEvent(EventType::SIZE_BEGIN, "", -1, -1);
				size_t ret = m_file.size();

				invokeEvent(EventType::SIZE_FINISHED, "", (int32)ret, -1);
				return ret;
			}


			bool seek(SeekMode base, size_t pos) override
			{
				invokeEvent(EventType::SEEK_BEGIN, "", (int32)pos, base);
				bool ret = m_file.seek(base, pos);

				invokeEvent(EventType::SEEK_FINISHED, "", (int32)ret, base);
				return ret;
			}


			size_t pos() override
			{
				invokeEvent(EventType::POS_BEGIN, "", -1, -1);
				size_t ret = m_file.pos();

				invokeEvent(EventType::POS_FINISHED, "", (int32)ret, -1);
				return ret;
			}


		private:
			EventsFile& operator= (const EventsFile& rhs);

			void invokeEvent(EventType type, const char* path, int32 ret, int32 param)
			{
				Event event;
				event.type = type;
				event.handle = uintptr(this);
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
			LUMIX_DELETE(m_allocator, file);
		}


		IFile* FileEventsDevice::createFile(IFile* child)
		{
			return LUMIX_NEW(m_allocator, EventsFile)(*child, *this, OnEvent);
		}
	} // namespace FS
} // ~namespace Lumix

#endif //FILE_EVENT_DEVICE
