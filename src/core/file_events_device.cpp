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

			virtual bool open(const char* path, Mode mode) LUX_OVERRIDE
			{
				m_cb.invoke(Event::OPEN_BEGIN, path, 0);
				bool ret = m_file.open(path, mode);
				m_cb.invoke(Event::OPEN_END, path, ret ? 1 : 0);
				return ret;
			}

			virtual void close() LUX_OVERRIDE
			{
				m_cb.invoke(Event::CLOSE_BEGIN, NULL, 0);
				m_file.close();
				m_cb.invoke(Event::CLOSE_END, NULL, 0);
			}

			virtual bool read(void* buffer, size_t size) LUX_OVERRIDE
			{
				m_cb.invoke(Event::READ_BEGIN, NULL, 0);
				bool ret = m_file.read(buffer, size);
				m_cb.invoke(Event::READ_END, NULL, ret ? 1 : 0);
				return ret;
			}

			virtual bool write(const void* buffer, size_t size) LUX_OVERRIDE
			{
				m_cb.invoke(Event::WRITE_BEGIN, NULL, 0);
				bool ret = m_file.write(buffer, size);
				m_cb.invoke(Event::WRITE_END, NULL, ret ? 1 : 0);
				return ret;
			}

			virtual const void* getBuffer() const LUX_OVERRIDE
			{
				return NULL;
			}

			virtual size_t size() LUX_OVERRIDE
			{
				m_cb.invoke(Event::SIZE_BEGIN, NULL, 0);
				size_t ret = m_file.size();
				m_cb.invoke(Event::SIZE_END, NULL, ret);
				return ret;
			}

			virtual size_t seek(SeekMode base, size_t pos) LUX_OVERRIDE
			{
				m_cb.invoke(Event::SEEK_BEGIN, NULL, 0);
				size_t ret = m_file.seek(base, pos);
				m_cb.invoke(Event::SEEK_END, NULL, ret);
				return ret;
			}

			virtual size_t pos() LUX_OVERRIDE
			{
				m_cb.invoke(Event::POS_BEGIN, NULL, 0);
				size_t ret = m_file.pos();
				m_cb.invoke(Event::POS_END, NULL, ret);
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