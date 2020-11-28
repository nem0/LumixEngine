#pragma once


#include "engine/lumix.h"


#ifdef LUMIX_PVS_STUDIO_BUILD
	#define LUMIX_FATAL(cond) { false ? (void)(cond) : (void)0; }
#else
	#define LUMIX_FATAL(cond) Lumix::fatal((cond), #cond);
#endif


namespace Lumix
{

struct Log;
struct Path;
template <typename T> struct DelegateList;

enum class LogLevel {
	INFO,
	WARNING,
	ERROR,

	COUNT
};

using LogCallback = DelegateList<void (LogLevel, const char*)>;

LUMIX_ENGINE_API void fatal(bool cond, const char* msg);
LUMIX_ENGINE_API void log(LogLevel level, const char* system);
LUMIX_ENGINE_API LogCallback& getLogCallback();

namespace detail {
	LUMIX_ENGINE_API void addLog(const char* val);
	LUMIX_ENGINE_API void addLog(const Path& val);
	LUMIX_ENGINE_API void addLog(u64 val);
	LUMIX_ENGINE_API void addLog(u32 val);
	LUMIX_ENGINE_API void addLog(i32 val);
	LUMIX_ENGINE_API void addLog(float val);
	LUMIX_ENGINE_API void emitLog(LogLevel level);
}

template <typename... T> void log(LogLevel level, const T&... args) {
	int tmp[] = { (detail::addLog(args), 0) ... };
	(void)tmp;
	detail::emitLog(level);
}

template <typename... T> void logInfo(const T&... args) { log(LogLevel::INFO, args...); }
template <typename... T> void logWarning(const T&... args) { log(LogLevel::WARNING, args...); }
template <typename... T> void logError(const T&... args) { log(LogLevel::ERROR, args...); }


} // namespace Lumix


