#pragma once

#define FILE_EVENT_DEVICE
#ifdef FILE_EVENT_DEVICE

#include "core/lumix.h"
#include "core/fs/ifile_device.h"

#include "core/delegate.h"

namespace Lumix
{
	class IAllocator;

	namespace FS
	{
		enum class EventType
		{
			OPEN_BEGIN = 0,
			OPEN_FINISHED,
			CLOSE_BEGIN,
			CLOSE_FINISHED,
			READ_BEGIN,
			READ_FINISHED,
			WRITE_BEGIN,
			WRITE_FINISHED,
			SIZE_BEGIN,
			SIZE_FINISHED,
			SEEK_BEGIN,
			SEEK_FINISHED,
			POS_BEGIN,
			POS_FINISHED
		};

		struct Event
		{
			EventType type;
			uintptr_t handle;
			const char* path;
			int32_t ret;
			int32_t param;
		};	

		class LUMIX_CORE_API FileEventsDevice : public IFileDevice
		{
		public:
			FileEventsDevice(IAllocator& allocator) : m_allocator(allocator) {}

			typedef Delegate<void(const Event&)>  EventCallback;

			EventCallback OnEvent;

			virtual void destroyFile(IFile* file) override;
			virtual IFile* createFile(IFile* child) override;

			const char* name() const { return "events"; }
		
		private:
			IAllocator& m_allocator;
		};
	} // namespace FS
} // ~namespace Lumix

#endif //FILE_EVENT_DEVICE
