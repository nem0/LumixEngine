#include "engine/array.h"
#include "engine/fibers.h"
#include "engine/hash_map.h"
#include "engine/default_allocator.h"
#include "engine/timer.h"
#include "engine/mt/atomic.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/mt/thread.h"
#include "profiler.h"
#include <cstring>

#define INITGUID
#include <Windows.h>
#include <evntcons.h>
#include <evntrace.h>
#include <thread>
#include <cassert>

namespace Lumix
{


namespace Profiler
{


struct ThreadContext
{
	ThreadContext(IAllocator& allocator) 
		: buffer(allocator)
		, open_blocks(allocator)
	{
		buffer.resize(1024 * 512);
		open_blocks.reserve(64);
	}

	Array<const char*> open_blocks;
	Array<u8> buffer;
	uint begin = 0;
	uint end = 0;
	uint rows = 0;
	bool open = false;
	MT::SpinMutex mutex;
	StaticString<64> name;
	bool show_in_profiler = false;
	u32 thread_id;
};


// TODO this has to be defined somewhere
#define SWITCH_CONTEXT_OPCODE 36

#pragma pack(1)
	struct TraceProps
	{
		EVENT_TRACE_PROPERTIES base;
		char name[sizeof(KERNEL_LOGGER_NAME) + 1];
	};
#pragma pack()


// https://docs.microsoft.com/en-us/windows/desktop/etw/cswitch
struct CSwitch
{
	uint32_t                 NewThreadId;
	uint32_t                 OldThreadId;
	int8_t             NewThreadPriority;
	int8_t             OldThreadPriority;
	uint8_t               PreviousCState;
	int8_t                     SpareByte;
	int8_t           OldThreadWaitReason;
	int8_t             OldThreadWaitMode;
	int8_t                OldThreadState;
	int8_t   OldThreadWaitIdealProcessor;
	uint32_t           NewThreadWaitTime;
	uint32_t                    Reserved;
};


struct TraceTask : MT::Task {
	TraceTask(IAllocator& allocator);

	int task() override;
	static void callback(PEVENT_RECORD event);

	TRACEHANDLE open_handle;
};


static struct Instance
{
	Instance()
		: contexts(allocator)
		, timer(Timer::create(allocator))
		, trace_task(allocator)
		, global_context(allocator)
	{
		startTrace();
	}


	~Instance()
	{
		CloseTrace(trace_task.open_handle);
		trace_task.destroy();
		Timer::destroy(timer);
	}


	static void startTrace()
	{
		static TRACEHANDLE trace_handle;
		static TraceProps props = {};
		props.base.Wnode.BufferSize = sizeof(props);
		props.base.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
		props.base.Wnode.ClientContext = 1;
		props.base.Wnode.Guid = SystemTraceControlGuid;
		props.base.LoggerNameOffset = sizeof(props.base);
		props.base.EnableFlags = EVENT_TRACE_FLAG_CSWITCH;
		props.base.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
		strcpy_s(props.name, KERNEL_LOGGER_NAME);

		TraceProps tmp = props;
		ControlTrace(NULL, KERNEL_LOGGER_NAME, &tmp.base, EVENT_TRACE_CONTROL_STOP);
		ULONG res = StartTrace(&trace_handle, KERNEL_LOGGER_NAME, &props.base);
		switch (res) {
		case ERROR_ALREADY_EXISTS:
		case ERROR_ACCESS_DENIED:
		case ERROR_BAD_LENGTH:
		default:
			g_instance.context_switches_enabled = false;
			break;
		case ERROR_SUCCESS:
			g_instance.context_switches_enabled = true;
			break;
		}

		static EVENT_TRACE_LOGFILE trace = {};
		trace.LoggerName = (char*)KERNEL_LOGGER_NAME; // TODO const cast wtf
		trace.ProcessTraceMode = PROCESS_TRACE_MODE_RAW_TIMESTAMP | PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
		trace.EventRecordCallback = TraceTask::callback;
		g_instance.trace_task.open_handle = OpenTrace(&trace);
		g_instance.trace_task.create("Profiler trace", true);
	}


	ThreadContext* getThreadContext()
	{
		thread_local ThreadContext* ctx = [&](){
			ThreadContext* new_ctx = LUMIX_NEW(allocator, ThreadContext)(allocator);
			new_ctx->thread_id = MT::getCurrentThreadID();
			MT::SpinLock lock(mutex);
			contexts.push(new_ctx);
			return new_ctx;
		}();

		return ctx;
	}


	DefaultAllocator allocator;
	Array<ThreadContext*> contexts;
	MT::SpinMutex mutex;
	Timer* timer;
	bool paused = false;
	bool context_switches_enabled = false;
	u64 paused_time = 0;
	u64 last_frame_duration = 0;
	u64 last_frame_time = 0;
	volatile i32 fiber_wait_id = 0;
	TraceTask trace_task;
	ThreadContext global_context;
} g_instance;


template <typename T>
void write(ThreadContext& ctx, u64 timestamp, EventType type, const T& value)
{
	if (g_instance.paused && timestamp > g_instance.paused_time) return;

	#pragma pack(1)
		struct {
			EventHeader header;
			T value;
		} v;
	#pragma pack()
	v.header.type = type;
	v.header.size = sizeof(v);
	v.header.time = timestamp;
	v.value = value;

	MT::SpinLock lock(ctx.mutex);
	u8* buf = ctx.buffer.begin();
	const int buf_size = ctx.buffer.size();

	while (sizeof(v) + ctx.end - ctx.begin > buf_size) {
		const u8 size = buf[ctx.begin % buf_size];
		ctx.begin += size;
	}

	const uint lend = ctx.end % buf_size;
	if (buf_size - lend >= sizeof(v)) {
		memcpy(buf + lend, &v, sizeof(v));
	}
	else {
		memcpy(buf + lend, &v, buf_size - lend);
		memcpy(buf, ((u8*)&v) + buf_size - lend, sizeof(v) - (buf_size - lend));
	}

	ctx.end += sizeof(v);
};

template <typename T>
void write(ThreadContext& ctx, EventType type, const T& value)
{
	if (g_instance.paused) return;

#pragma pack(1)
	struct {
		EventHeader header;
		T value;
	} v;
#pragma pack()
	v.header.type = type;
	v.header.size = sizeof(v);
	v.header.time = Timer::getRawTimestamp();
	v.value = value;

	MT::SpinLock lock(ctx.mutex);
	u8* buf = ctx.buffer.begin();
	const int buf_size = ctx.buffer.size();

	while (sizeof(v) + ctx.end - ctx.begin > buf_size) {
		const u8 size = buf[ctx.begin % buf_size];
		ctx.begin += size;
	}

	const uint lend = ctx.end % buf_size;
	if (buf_size - lend >= sizeof(v)) {
		memcpy(buf + lend, &v, sizeof(v));
	}
	else {
		memcpy(buf + lend, &v, buf_size - lend);
		memcpy(buf, ((u8*)&v) + buf_size - lend, sizeof(v) - (buf_size - lend));
	}

	ctx.end += sizeof(v);
};


void write(ThreadContext& ctx, EventType type, const u8* data, int size)
{
	if (g_instance.paused) return;

	EventHeader header;
	header.type = type;
	ASSERT(sizeof(header) + size <= 0xffff);
	header.size = u16(sizeof(header) + size);
	header.time = Timer::getRawTimestamp();

	MT::SpinLock lock(ctx.mutex);
	u8* buf = ctx.buffer.begin();
	const uint buf_size = ctx.buffer.size();

	while (header.size + ctx.end - ctx.begin > buf_size) {
		const u8 size = buf[ctx.begin % buf_size];
		ctx.begin += size;
	}

	auto cpy = [&](const u8* ptr, uint size) {
		const uint lend = ctx.end % buf_size;
		if (buf_size - lend >= size) {
			memcpy(buf + lend, ptr, size);
		}
		else {
			memcpy(buf + lend, ptr, buf_size - lend);
			memcpy(buf, ((u8*)ptr) + buf_size - lend, size - (buf_size - lend));
		}

		ctx.end += size;
	};

	cpy((u8*)&header, sizeof(header));
	cpy(data, size);
};


TraceTask::TraceTask(IAllocator& allocator)
	: MT::Task(allocator)
{}


int TraceTask::task() {
	ProcessTrace(&open_handle, 1, nullptr, nullptr);
	return 0;
}


void TraceTask::callback(PEVENT_RECORD event) {
	if (event->EventHeader.EventDescriptor.Opcode != SWITCH_CONTEXT_OPCODE) return;
	if (sizeof(CSwitch) != event->UserDataLength) return;

	const CSwitch* cs = reinterpret_cast<CSwitch*>(event->UserData);
	ContextSwitchRecord rec;
	rec.timestamp = event->EventHeader.TimeStamp.QuadPart;
	rec.new_thread_id = cs->NewThreadId;
	rec.old_thread_id = cs->OldThreadId;
	rec.reason = cs->OldThreadWaitReason;
	write(g_instance.global_context, rec.timestamp, Profiler::EventType::CONTEXT_SWITCH, rec);
};


void pushInt(const char* key, int value)
{
	ThreadContext* ctx = g_instance.getThreadContext();
	IntRecord r;
	r.key = key;
	r.value = value;
	write(*ctx, EventType::INT, (u8*)&r, sizeof(r));
}



void pushString(const char* value)
{
	ThreadContext* ctx = g_instance.getThreadContext();
	write(*ctx, EventType::STRING, (u8*)value, stringLength(value) + 1);
}


void blockColor(u8 r, u8 g, u8 b)
{
	const u32 color = 0xff000000 + r + (g << 8) + (b << 16);
	ThreadContext* ctx = g_instance.getThreadContext();
	write(*ctx, EventType::BLOCK_COLOR, color);
}


void beginBlock(const char* name)
{
	ThreadContext* ctx = g_instance.getThreadContext();
	ctx->open_blocks.push(name);
	write(*ctx, EventType::BEGIN_BLOCK, name);
}


void beginGPUBlock(const char* name, u64 timestamp, i64 profiler_link)
{
	GPUBlock data;
	data.timestamp = timestamp;
	copyString(data.name, name);
	data.profiler_link = profiler_link;
	write(g_instance.global_context, EventType::BEGIN_GPU_BLOCK, data);
}


void endGPUBlock(u64 timestamp)
{
	write(g_instance.global_context, EventType::END_GPU_BLOCK, timestamp);
}


i64 createNewLinkID()
{
	static i64 counter = 0;
	return MT::atomicIncrement(&counter);
}


void link(i64 link)
{
	ThreadContext* ctx = g_instance.getThreadContext();
	write(*ctx, EventType::LINK, link);
}


void gpuFrame()
{
	write(g_instance.global_context, EventType::GPU_FRAME, (int)0);

}


float getLastFrameDuration()
{
	return float(g_instance.last_frame_duration / double(frequency()));
}


void beforeFiberSwitch()
{
	ThreadContext* ctx = g_instance.getThreadContext();
	while(!ctx->open_blocks.empty()) {
		write(*ctx, EventType::END_BLOCK, 0);
		ctx->open_blocks.pop();
	}
}


int getOpenBlocksSize()
{
	ThreadContext* ctx = g_instance.getThreadContext();
	const int count = ctx->open_blocks.size();
	return count * sizeof(const char*) + sizeof(count);
}


void pushJobInfo(u32 signal_on_finish, u32 precondition)
{
	JobRecord r;
	r.signal_on_finish = signal_on_finish;
	r.precondition = precondition;
	ThreadContext* ctx = g_instance.getThreadContext();
	write(*ctx, EventType::JOB_INFO, r);
}


i32 beginFiberWait(u32 job_system_signal, void* open_blocks)
{
	FiberWaitRecord r;
	r.id = MT::atomicIncrement(&g_instance.fiber_wait_id);
	r.job_system_signal = job_system_signal;

	ThreadContext* ctx = g_instance.getThreadContext();
	const int count = ctx->open_blocks.size();
	memcpy(open_blocks, &count, sizeof(count));
	memcpy((u8*)open_blocks + sizeof(count), ctx->open_blocks.begin(), count * sizeof(const char*));
	write(*ctx, EventType::BEGIN_FIBER_WAIT, r);
	return r.id;
}


void endFiberWait(u32 job_system_signal, i32 wait_id, const void* open_blocks)
{
	ThreadContext* ctx = g_instance.getThreadContext();
	FiberWaitRecord r;
	r.id = wait_id;
	r.job_system_signal = job_system_signal;

	write(*ctx, EventType::END_FIBER_WAIT, r);
	int count;
	memcpy(&count, open_blocks, sizeof(count));
	
	u8* ptr = (u8*)open_blocks + sizeof(count);
	for (int i = 0; i < count; ++i) {
		const char* name;
		memcpy(&name, ptr, sizeof(name));
		ptr += sizeof(name);
		beginBlock(name);
	}
}


void endBlock()
{
	ThreadContext* ctx = g_instance.getThreadContext();
	if(!ctx->open_blocks.empty()) {
		ctx->open_blocks.pop();
		write(*ctx, EventType::END_BLOCK, 0);
	}
}


u64 frequency()
{
	return g_instance.timer->getFrequency();
}


bool contextSwitchesEnabled()
{
	return g_instance.context_switches_enabled;
}


void frame()
{
	const u64 n = Timer::getRawTimestamp();
	if (g_instance.last_frame_time != 0) {
		g_instance.last_frame_duration = n - g_instance.last_frame_time;
	}
	g_instance.last_frame_time = n;
	write(g_instance.global_context, EventType::FRAME, 0);
}


void showInProfiler(bool show)
{
	ThreadContext* ctx = g_instance.getThreadContext();
	MT::SpinLock lock(ctx->mutex);

	ctx->show_in_profiler = show;
}


void setThreadName(const char* name)
{
	ThreadContext* ctx = g_instance.getThreadContext();
	MT::SpinLock lock(ctx->mutex);

	ctx->name = name;
}


GlobalState::GlobalState()
{
	g_instance.mutex.lock();
}


GlobalState::~GlobalState()
{
	ASSERT(local_readers_count == 0);
	g_instance.mutex.unlock();
}


int GlobalState::threadsCount() const
{
	return g_instance.contexts.size();
}


const char* GlobalState::getThreadName(int idx) const
{
	ThreadContext* ctx = g_instance.contexts[idx];
	MT::SpinLock lock(ctx->mutex);
	return ctx->name;
}


ThreadState::ThreadState(GlobalState& reader, int thread_idx)
	: reader(reader)
	, thread_idx(thread_idx)
{
	++reader.local_readers_count;
	ThreadContext& ctx = thread_idx >= 0 ? *g_instance.contexts[thread_idx] : g_instance.global_context;

	ctx.mutex.lock();
	buffer = ctx.buffer.begin();
	buffer_size = ctx.buffer.size();
	begin = ctx.begin;
	end = ctx.end;
	thread_id = ctx.thread_id;
	name = ctx.name;
	open = ctx.open;
	rows = ctx.rows;
	show = ctx.show_in_profiler;
}


ThreadState::~ThreadState()
{
	ThreadContext& ctx = thread_idx >= 0 ? *g_instance.contexts[thread_idx] : g_instance.global_context;
	ctx.open = open;
	ctx.rows = rows;
	ctx.show_in_profiler = show;
	ctx.mutex.unlock();
	--reader.local_readers_count;
}


void pause(bool paused)
{
	g_instance.paused = paused;
	if (paused) g_instance.paused_time = Timer::getRawTimestamp();
}


} // namespace Lumix


} //	namespace Profiler
