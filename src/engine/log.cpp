#include "engine/allocators.h"
#include "engine/crt.h"
#include "engine/delegate_list.h"
#include "engine/log.h"
#include "engine/path.h"
#include "engine/stream.h"
#include "engine/string.h"


namespace Lumix
{

void fatal(bool cond, const char* msg)
{
	if (!cond) {
		logError(msg, " is false.");
		abort();
	}
}

struct Logger {
	Logger() : callback(allocator) {}

	DefaultAllocator allocator;
	LogCallback callback;
};

static Logger g_logger;

struct Log {
	Log() : message(allocator) { message.reserve(4096); }
	DefaultAllocator allocator;
	OutputMemoryStream message;
};

thread_local Log g_log;

namespace detail {
	void addLog(const char* val) { g_log.message << val; }
	void addLog(const Path& val) { g_log.message << val.c_str(); }
	void addLog(u32 val) { g_log.message << val; }
	void addLog(i32 val) { g_log.message << val; }
	void addLog(float val) { g_log.message << val; }

	void emitLog(LogLevel level) {
		g_log.message.write('\0');
		g_logger.callback.invoke(level, (const char*)g_log.message.data());
		g_log.message.clear();
	}
}

LogCallback& getLogCallback() { return g_logger.callback; }
void log(LogLevel level, const char* msg) { g_logger.callback.invoke(level, msg); }

} // namespace Lumix
