#pragma once


#include "engine/array.h"
#include "engine/lumix.h"
#include "engine/mt/sync.h"
#include "engine/mt/thread.h"


namespace Lumix
{


template <typename T> class DelegateList;


namespace Profiler
{


struct ThreadContext
{
	ThreadContext(IAllocator& allocator) 
		: buffer(allocator)
	{
		buffer.resize(1024 * 512);
	}

	int open_blocks_count = 0;
	Array<u8> buffer;
	uint begin = 0;
	uint end = 0;
	uint rows = 0;
	bool open = false;
	MT::SpinMutex mutex;
	StaticString<64> name;
};


enum class EventType : u8
{
	BEGIN_BLOCK,
	BLOCK_COLOR,
	END_BLOCK,
	FRAME,
	STRING
};


#pragma pack(1)
struct EventHeader
{
	u16 size;
	EventType type;
	u64 time;
};
#pragma pack()


LUMIX_ENGINE_API void setThreadName(const char* name);

LUMIX_ENGINE_API u64 now();
LUMIX_ENGINE_API u64 frequency();
LUMIX_ENGINE_API void pause(bool paused);

LUMIX_ENGINE_API void beginBlock(const char* name);
LUMIX_ENGINE_API void blockColor(u8 r, u8 g, u8 b);
LUMIX_ENGINE_API void endBlock();
LUMIX_ENGINE_API void frame();
LUMIX_ENGINE_API void recordString(const char* value);

LUMIX_ENGINE_API void beginFiberSwitch();
LUMIX_ENGINE_API float getLastFrameDuration();

LUMIX_ENGINE_API Array<ThreadContext*>& lockContexts();
LUMIX_ENGINE_API void unlockContexts();

struct Scope
{
	explicit Scope(const char* name) { beginBlock(name); }
	~Scope() { endBlock(); }
};


} // namespace Profiler


#define PROFILE_INT(...)
#define PROFILE_FUNCTION() Profiler::Scope profile_scope(__FUNCTION__);
#define PROFILE_BLOCK(name) Profiler::Scope profile_scope(name);


} // namespace Lumix
