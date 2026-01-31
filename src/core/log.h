#pragma once

#include "core.h"

namespace black {

enum class LogLevel {
	INFO,
	WARNING,
	ERROR,

	COUNT
};

struct StringView;

namespace detail {
	BLACK_CORE_API void addLog(StringView val);
	BLACK_CORE_API void addLog(u64 val);
	BLACK_CORE_API void addLog(i64 val);
	BLACK_CORE_API void addLog(u32 val);
	BLACK_CORE_API void addLog(i32 val);
	BLACK_CORE_API void addLog(float val);
	BLACK_CORE_API void emitLog(LogLevel level);
	BLACK_CORE_API void lock();
	BLACK_CORE_API void unlock();
	template <typename... T> void log(LogLevel level, const T&... args) {
		int tmp[] = { (addLog(args), 0) ... };
		(void)tmp;
		emitLog(level);
	}
} // namespace detail

template <typename... T> void logInfo(const T&... args) { detail::log(LogLevel::INFO, args...); }
template <typename... T> void logWarning(const T&... args) { detail::log(LogLevel::WARNING, args...); }
template <typename... T> void logError(const T&... args) { detail::log(LogLevel::ERROR, args...); }

} // namespace black


