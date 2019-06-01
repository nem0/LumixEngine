#pragma once


#include "engine/array.h"
#include "engine/lumix.h"
#include "engine/mt/sync.h"
#include "engine/mt/thread.h"


namespace Lumix
{


template <typename T> class DelegateList;


namespace Fiber { enum class SwitchReason; }


namespace Profiler
{


struct ContextSwitchRecord
{
	u32 old_thread_id;
	u32 new_thread_id;
	u64 timestamp;
	i8 reason;
};


struct IntRecord
{
	const char* key;
	int value;
};


struct JobRecord
{
	u32 signal_on_finish;
	u32 precondition;
};


struct FiberWaitRecord
{
	i32 id;
	u32 job_system_signal;
};


struct ThreadContext
{
	ThreadContext(IAllocator& allocator) 
		: buffer(allocator)
		, open_blocks(allocator)
	{
		buffer.resize(1024 * 512);
		open_blocks.reserve(64);
	}

	int open_blocks_count = 0;
	Array<const char*> open_blocks;
	Array<u8> buffer;
	uint begin = 0;
	uint end = 0;
	uint rows = 0;
	bool open = false;
	MT::SpinMutex mutex;
	StaticString<64> name;
	u32 thread_id;
};


enum class EventType : u8
{
	BEGIN_BLOCK,
	BLOCK_COLOR,
	END_BLOCK,
	FRAME,
	STRING,
	INT,
	BEGIN_FIBER_WAIT,
	END_FIBER_WAIT,
	CONTEXT_SWITCH,
	JOB_INFO,
	BEGIN_GPU_BLOCK,
	END_GPU_BLOCK,
	GPU_FRAME
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

LUMIX_ENGINE_API u64 frequency();
LUMIX_ENGINE_API void pause(bool paused);

LUMIX_ENGINE_API void beginBlock(const char* name);
LUMIX_ENGINE_API void blockColor(u8 r, u8 g, u8 b);
LUMIX_ENGINE_API void endBlock();
LUMIX_ENGINE_API void pushJobInfo(u32 signal_on_finish, u32 precondition);
LUMIX_ENGINE_API void frame();
LUMIX_ENGINE_API void recordString(const char* value);
LUMIX_ENGINE_API void recordInt(const char* key, int value);

LUMIX_ENGINE_API void beginGPUBlock(const char* name, u64 timestamp);
LUMIX_ENGINE_API void endGPUBlock(u64 timestamp);
LUMIX_ENGINE_API void gpuFrame();

LUMIX_ENGINE_API void beforeFiberSwitch();
LUMIX_ENGINE_API int getOpenBlocksSize();
LUMIX_ENGINE_API i32 beginFiberWait(u32 job_system_signal, void* open_blocks);
LUMIX_ENGINE_API void endFiberWait(u32 job_system_signal, i32 wait_id, const void* open_blocks);
LUMIX_ENGINE_API float getLastFrameDuration();

LUMIX_ENGINE_API bool contextSwitchesEnabled();
LUMIX_ENGINE_API ThreadContext& getGlobalContext();
LUMIX_ENGINE_API Array<ThreadContext*>& lockContexts();
LUMIX_ENGINE_API void unlockContexts();

struct Scope
{
	explicit Scope(const char* name) { beginBlock(name); }
	~Scope() { endBlock(); }
};


} // namespace Profiler


#define PROFILE_FUNCTION() Profiler::Scope profile_scope(__FUNCTION__);
#define PROFILE_BLOCK(name) Profiler::Scope profile_scope(name);


} // namespace Lumix
