#pragma once

#define FILE_EVENT_DEVICE
#ifdef FILE_EVENT_DEVICE

#include "engine/lumix.h"
#include "engine/core/fs/ifile_device.h"

#include "engine/core/delegate.h"

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
			uintptr handle;
			const char* path;
			int32 ret;
			int32 param;
		};	

		class LUMIX_ENGINE_API FileEventsDevice : public IFileDevice
		{
		public:
			FileEventsDevice(IAllocator& allocator) : m_allocator(allocator) {}

			typedef Delegate<void(const Event&)>  EventCallback;

			EventCallback OnEvent;

			void destroyFile(IFile* file) override;
			IFile* createFile(IFile* child) override;

			const char* name() const override { return "events"; }
		
		private:
			IAllocator& m_allocator;
		};
	} // namespace FS
} // ~namespace Lumix

#endif //FILE_EVENT_DEVICE
