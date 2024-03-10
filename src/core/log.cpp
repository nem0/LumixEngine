#include "core/allocators.h"
#include "core/crt.h"
#include "core/delegate_list.h"
#include "core/log.h"
#include "core/path.h"
#include "core/stream.h"
#include "core/string.h"
#include "core/sync.h"


namespace Lumix
{


namespace detail {
	struct Logger {
		Logger() : callback(getGlobalAllocator()) {}

		Mutex mutex;
		LogCallback callback;
	};

	struct Log {
		Log() : message(getGlobalAllocator()) { message.reserve(4096); }
		OutputMemoryStream message;
	};

	static Logger g_logger;
	thread_local Log g_log;

	void addLog(StringView val) { g_log.message << val; }
	void addLog(u32 val) { g_log.message << val; }
	void addLog(u64 val) { g_log.message << val; }
	void addLog(i32 val) { g_log.message << val; }
	void addLog(float val) { g_log.message << val; }

	void lock() { g_logger.mutex.enter(); }
	void unlock() { g_logger.mutex.exit(); }

	void emitLog(LogLevel level) {
		g_log.message.write('\0');
		{
			MutexGuard lock(g_logger.mutex);
			g_logger.callback.invoke(level, (const char*)g_log.message.data());
		}
		g_log.message.clear();
	}

	LogCallback& getLogCallback() { return g_logger.callback; }
} // namespace detail


} // namespace Lumix
