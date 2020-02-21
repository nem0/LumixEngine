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
template <typename T> struct DelegateList;

enum class LogLevel {
	INFO,
	WARNING,
	ERROR,

	COUNT
};

using LogCallback = DelegateList<void (LogLevel, const char*, const char*)>;


struct LUMIX_ENGINE_API LogProxy {
public:
	LogProxy(Log* log, const char* system);
	~LogProxy();

	LogProxy& operator <<(const char* message);
	LogProxy& operator <<(float message);
	LogProxy& operator <<(i32 message);
	LogProxy& operator <<(u32 message);
	LogProxy& operator <<(u64 message);
	LogProxy& operator <<(const struct String& path);
	LogProxy& operator <<(const struct Path& path);
		
private:
	Log* log;
	const char* system;

	LogProxy(const LogProxy&) = delete;
	void operator = (const LogProxy&) = delete;
};


LUMIX_ENGINE_API void fatal(bool cond, const char* msg);
LUMIX_ENGINE_API LogProxy logInfo(const char* system);
LUMIX_ENGINE_API LogProxy logWarning(const char* system);
LUMIX_ENGINE_API LogProxy logError(const char* system);
LUMIX_ENGINE_API LogCallback& getLogCallback();


} // namespace Lumix


