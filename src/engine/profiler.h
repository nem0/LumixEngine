#pragma once

#include "engine/lumix.h"

namespace Lumix {

struct OutputMemoryStream;

namespace profiler {
// writing API

LUMIX_ENGINE_API void pause(bool paused);

LUMIX_ENGINE_API void setThreadName(const char* name);
LUMIX_ENGINE_API void showInProfiler(bool show);

LUMIX_ENGINE_API void beginBlock(const char* name_literal);
LUMIX_ENGINE_API void continueBlock(i32 block_id);
LUMIX_ENGINE_API void blockColor(u8 r, u8 g, u8 b);
LUMIX_ENGINE_API void endBlock();
LUMIX_ENGINE_API void frame();
LUMIX_ENGINE_API void pushJobInfo(i32 signal_on_finish);
LUMIX_ENGINE_API void pushString(const char* value);
LUMIX_ENGINE_API void pushInt(const char* key_literal, int value);

LUMIX_ENGINE_API void beginGPUBlock(const char* name, u64 timestamp, i64 profiler_link);
LUMIX_ENGINE_API void endGPUBlock(u64 timestamp);
LUMIX_ENGINE_API void gpuStats(u64 primitives_generated);
LUMIX_ENGINE_API void gpuMemStats(u64 total, u64 current, u64 dedicated, u64 buffer_mem, u64 texture_mem, u64 render_target_mem);
LUMIX_ENGINE_API void gpuFrame();
LUMIX_ENGINE_API void link(i64 link);
LUMIX_ENGINE_API i64 createNewLinkID();
LUMIX_ENGINE_API void serialize(OutputMemoryStream& blob);

struct FiberSwitchData {
	i32 id;
	i32 blocks[16];
	u32 count;
	i32 signal;
};

LUMIX_ENGINE_API void beforeFiberSwitch();
LUMIX_ENGINE_API void signalTriggered(i32 job_system_signal);
LUMIX_ENGINE_API FiberSwitchData beginFiberWait(i32 job_system_signal, bool is_mutex);
LUMIX_ENGINE_API void endFiberWait(const FiberSwitchData& switch_data);
LUMIX_ENGINE_API float getLastFrameDuration();

struct Scope
{
	explicit Scope(const char* name_literal) { beginBlock(name_literal); }
	~Scope() { endBlock(); }
};


// reading API

LUMIX_ENGINE_API bool contextSwitchesEnabled();
LUMIX_ENGINE_API u64 frequency();


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

struct IntRecord
{
	const char* key;
	int value;
};


struct JobRecord
{
	i32 signal_on_finish;
};


struct FiberWaitRecord
{
	i32 id;
	i32 job_system_signal;
	bool is_mutex;
};


struct GPUBlock
{
	char name[32];
	u64 timestamp;
	i64 profiler_link;
};

struct GPUMemStatsBlock
{
	u64 total;
	u64 current;
	u64 dedicated;
	u64 buffer_mem;
	u64 texture_mem;
	u64 render_target_mem;
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
	GPU_FRAME,
	GPU_MEM_STATS,
	LINK,
	PAUSE,
	GPU_STATS,
	CONTINUE_BLOCK,
	SIGNAL_TRIGGERED
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
