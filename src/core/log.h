#pragma once

#include "core.h"

namespace Lumix {

enum class LogLevel {
	INFO,
	WARNING,
	ERROR,

	COUNT
};

struct StringView;

namespace detail {
	LUMIX_CORE_API void addLog(StringView val);
	LUMIX_CORE_API void addLog(u64 val);
	LUMIX_CORE_API void addLog(i64 val);
	LUMIX_CORE_API void addLog(u32 val);
	LUMIX_CORE_API void addLog(i32 val);
	LUMIX_CORE_API void addLog(float val);
	LUMIX_CORE_API void emitLog(LogLevel level);
	LUMIX_CORE_API void lock();
	LUMIX_CORE_API void unlock();
	template <typename... T> void log(LogLevel level, const T&... args) {
		int tmp[] = { (addLog(args), 0) ... };
		(void)tmp;
		emitLog(level);
	}
} // namespace detail

template <typename... T> void logInfo(const T&... args) { detail::log(LogLevel::INFO, args...); }
template <typename... T> void logWarning(const T&... args) { detail::log(LogLevel::WARNING, args...); }
template <typename... T> void logError(const T&... args) { detail::log(LogLevel::ERROR, args...); }

} // namespace Lumix


