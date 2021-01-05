#include "engine/allocators.h"
#include "engine/crt.h"
#include "engine/delegate_list.h"
#include "engine/log.h"
#include "engine/path.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "engine/sync.h"


namespace Lumix
{

void fatal(bool cond, const char* msg)
{
	if (!cond) {
		logError(msg, " is false.");
		abort();
	}
}

namespace detail {
	struct Logger {
		Logger() : callback(allocator) {}

		Mutex mutex;
		DefaultAllocator allocator;
		LogCallback callback;
	};

	struct Log {
		Log() : message(allocator) { message.reserve(4096); }
		DefaultAllocator allocator;
		OutputMemoryStream message;
	};

	static Logger g_logger;
	thread_local Log g_log;

	void addLog(const char* val) { g_log.message << val; }
	void addLog(const Path& val) { g_log.message << val.c_str(); }
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
