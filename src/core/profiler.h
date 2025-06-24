#pragma once

#include "core.h"

namespace Lumix {

struct IAllocator;
struct OutputMemoryStream;
template <typename T> struct Span;

namespace profiler {
// writing API

LUMIX_CORE_API void init(IAllocator& allocator);
LUMIX_CORE_API void shutdown();
LUMIX_CORE_API void pause(bool paused);

LUMIX_CORE_API void setThreadName(const char* name);
LUMIX_CORE_API void showInProfiler(bool show);

LUMIX_CORE_API void beginBlock(const char* name_literal);
LUMIX_CORE_API void beginJob(i32 signal_on_finish);
LUMIX_CORE_API void blockColor(u32 abgr);
LUMIX_CORE_API void endBlock();
LUMIX_FORCE_INLINE void endJob() { endBlock(); }
LUMIX_CORE_API void frame();
LUMIX_CORE_API void pushString(const char* value);
LUMIX_CORE_API void pushInt(const char* key_literal, int value);
LUMIX_CORE_API void pushMutexEvent(u64 mutex_id, u64 begin_enter_time, u64 end_enter_time, u64 begin_exit_time, u64 end_exit_time);

enum { INVALID_COUNTER = 0xffFFffFF };
LUMIX_CORE_API u32 createCounter(const char* key_literal, float min);
LUMIX_CORE_API u32 getCounterHandle(const char* key, float* last_value = nullptr);
LUMIX_CORE_API void pushCounter(u32 counter, float value);

LUMIX_CORE_API void beginGPUBlock(const char* name, u64 timestamp, i64 profiler_link);
LUMIX_CORE_API void endGPUBlock(u64 timestamp);
LUMIX_CORE_API void gpuStats(u64 primitives_generated);
LUMIX_CORE_API void link(i64 link);
LUMIX_CORE_API i64 createNewLinkID();
LUMIX_CORE_API void serialize(OutputMemoryStream& blob);

struct FiberSwitchData {
	i32 id;
	i32 blocks[16];
	u32 count;
	i32 signal;
};

LUMIX_CORE_API void beforeFiberSwitch();
LUMIX_CORE_API void signalTriggered(i32 job_system_signal);
LUMIX_CORE_API FiberSwitchData beginFiberWait(i32 job_system_signal);
LUMIX_CORE_API void endFiberWait(const FiberSwitchData& switch_data);
LUMIX_CORE_API float getLastFrameDuration();

struct Scope
{
	explicit Scope(const char* name_literal) { beginBlock(name_literal); }
	~Scope() { endBlock(); }
};


// reading API

LUMIX_CORE_API u32 getOpenBlocks(Span<const char*> output);
LUMIX_CORE_API bool contextSwitchesEnabled();
LUMIX_CORE_API u64 frequency();

struct GPUScopeStats {
	const char* name;
	float min;
	float max;
	float avg;
};

LUMIX_CORE_API u32 getGPUScopeStats(Span<GPUScopeStats> out);

struct ContextSwitchRecord
{
	u32 old_thread_id;
	u32 new_thread_id;
	u64 timestamp;
	i8 reason;
};

struct BlockRecord {
	const char* name;
	i32 id;
};

struct Counter {
	char name[64];
	float min;
	float last_value = -1;
};

struct CounterRecord {
	u32 counter;
	float value;
};

struct IntRecord
{
	const char* key;
	int value;
};


struct JobRecord {
	i32 id;
	i32 signal_on_finish;
};


struct FiberWaitRecord
{
	i32 id;
	i32 job_system_signal;
};

struct MutexEvent {
	u64 mutex_id;
	u64 begin_enter;
	u64 end_enter;
	u64 begin_exit;
	u64 end_exit;
};

struct GPUBlock
{
	char name[32];
	u64 timestamp;
	i64 profiler_link;
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
	BEGIN_JOB,
	BEGIN_GPU_BLOCK,
	END_GPU_BLOCK,
	LINK,
	PAUSE,
	GPU_STATS,
	CONTINUE_BLOCK,
	SIGNAL_TRIGGERED,
	COUNTER,
	MUTEX_EVENT
};

#pragma pack(1)
struct EventHeader
{
	u16 size;
	EventType type;
	u64 time;
};
#pragma pack()

#define LUMIX_CONCAT2(a, b) a ## b
#define LUMIX_CONCAT(a, b) LUMIX_CONCAT2(a, b)

#define PROFILE_FUNCTION() profiler::Scope profile_scope(__FUNCTION__);
#define PROFILE_BLOCK(name) profiler::Scope LUMIX_CONCAT(profile_scope, __LINE__)(name);


} // namespace profiler
} // namespace Lumix
