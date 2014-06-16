#pragma once

#define FILE_EVENT_DEVICE
#ifdef FILE_EVENT_DEVICE

#include "core/lumix.h"
#include "core/fs/ifile_device.h"

#include "core/delegate.h"

namespace Lumix
{
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

		class LUX_CORE_API FileEventsDevice : public IFileDevice
		{
		public:
			typedef Delegate<void(const Event&)>  EventCallback;

			EventCallback OnEvent;

			virtual IFile* createFile(IFile* child) override;

			const char* name() const { return "events"; }
		};
	} // namespace FS
} // ~namespace Lumix

#endif //FILE_EVENT_DEVICE
