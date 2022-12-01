#pragma once

#include "engine/lumix.h"
#include "engine/delegate_list.h"

namespace Lumix {

struct Path;
struct String;
template <typename T> struct DelegateList;

enum class LogLevel {
	INFO,
	WARNING,
	ERROR,

	COUNT
};

namespace detail {
	using LogCallback = DelegateList<void (LogLevel, const char*)>;
	LUMIX_ENGINE_API void addLog(const char* val);
	LUMIX_ENGINE_API void addLog(const Path& val);
	LUMIX_ENGINE_API void addLog(const String& val);
	LUMIX_ENGINE_API void addLog(u64 val);
	LUMIX_ENGINE_API void addLog(u32 val);
	LUMIX_ENGINE_API void addLog(i32 val);
	LUMIX_ENGINE_API void addLog(float val);
	LUMIX_ENGINE_API void emitLog(LogLevel level);
	LUMIX_ENGINE_API void lock();
	LUMIX_ENGINE_API void unlock();
	LUMIX_ENGINE_API LogCallback& getLogCallback();
	template <typename... T> void log(LogLevel level, const T&... args) {
		int tmp[] = { (addLog(args), 0) ... };
		(void)tmp;
		emitLog(level);
	}
} // namespace detail

template <typename... T> void logInfo(const T&... args) { detail::log(LogLevel::INFO, args...); }
template <typename... T> void logWarning(const T&... args) { detail::log(LogLevel::WARNING, args...); }
template <typename... T> void logError(const T&... args) { detail::log(LogLevel::ERROR, args...); }
template <auto F> void registerLogCallback() { detail::lock(); detail::getLogCallback().bind<F>(); detail::unlock(); }
template <auto F> void unregisterLogCallback() { detail::lock(); detail::getLogCallback().unbind<F>(); detail::unlock(); }
template <auto F, typename T> void registerLogCallback(T* inst) { detail::lock(); detail::getLogCallback().bind<F>(inst); detail::unlock(); }
template <auto F, typename T> void unregisterLogCallback(T* inst) { detail::lock(); detail::getLogCallback().unbind<F>(inst); detail::unlock(); }

} // namespace Lumix


