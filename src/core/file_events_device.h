#pragma once

#define FILE_EVENT_DEVICE
#ifdef FILE_EVENT_DEVICE

#include "core/lux.h"
#include "core/ifile_device.h"

#include "core/delegate.h"

namespace Lux
{
	namespace FS
	{
		enum class Event
		{
			OPEN_BEGIN = 0,
			OPEN_END,
			CLOSE_BEGIN,
			CLOSE_END,
			READ_BEGIN,
			READ_END,
			WRITE_BEGIN,
			WRITE_END,
			SIZE_BEGIN,
			SIZE_END,
			SEEK_BEGIN,
			SEEK_END,
			POS_BEGIN,
			POS_END
		};

		typedef Delegate<void(Event, const char*, int32_t)>  EventCallback;

		class LUX_CORE_API FileEventsDevice : public IFileDevice
		{
		public:
			EventCallback OnEvent;

			virtual IFile* createFile(IFile* child) override;

			const char* name() const { return "events"; }
		};
	} // namespace FS
} // ~namespace Lux

#endif //FILE_EVENT_DEVICE
