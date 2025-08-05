#ifdef _WIN32
	#define INITGUID
	#define NOGDI
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
	#include <evntcons.h>
#endif

#include "core/atomic.h"
#include "core/array.h"
#include "core/command_line_parser.h"
#include "core/color.h"
#include "core/crt.h"
#include "core/debug.h"
#include "core/hash_map.h"
#include "core/math.h"
#include "core/string.h"
#include "core/sync.h"
#include "core/tag_allocator.h"
#include "core/thread.h"
#include "core/os.h"
#include "profiler.h"

namespace Lumix
{


namespace profiler
{


struct GPUScope {
	struct Pair {
		u64 begin;
		u64 end;
	};

	GPUScope(IAllocator& allocator)
		: name(allocator)
	{}

	void pushBegin(u64 timestamp) {
		if (write - read == lengthOf(pairs)) ++read;
		pairs[write % lengthOf(pairs)].begin = timestamp;
	}

	void pushEnd(u64 timestamp) {
		if (write - read == lengthOf(pairs)) ++read;
		pairs[write % lengthOf(pairs)].end = timestamp;
		++write;
	}

	String name;
	Pair pairs[100];
	u32 read = 0;
	u32 write = 0;
};

struct ThreadContext {
	ThreadContext(IAllocator& allocator) 
		: allocator(allocator)
	{
	}

	~ThreadContext() {
		Page* page = first_page;
		while (page) {
			Page* next = page->header.next;
			LUMIX_DELETE(allocator, page);
			page = next;
		}
	}

	struct OpenBlock {
		i32 id;
		const char* name;
	};

	struct Page {
		struct Header {
			Page* next = nullptr;
			u32 size = 0;
		};
		Header header;
		u8 buffer[4096 - sizeof(Header)];
	};

	IAllocator& allocator;
	OpenBlock open_block_stack[16] = {};
	u32 open_block_stack_size = 0;
	
	// we write to `tmp` until it's full, then we flush it to the `buffer`
	// tmp is only accessed by the thread that owns the context
	u8 tmp[sizeof(Page::buffer)];
	u32 tmp_pos = 0;
	
	// ring buffer, access only while holding the mutex
	// the ring buffer can be read by profiler UI
	Mutex mutex;
	Page* first_page = nullptr;
	Page* last_page = nullptr;
	u32 num_pages = 0;

	StaticString<64> thread_name;
	bool show_in_profiler = false;
	u32 thread_id;
};

#ifdef _WIN32
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
		u32                 NewThreadId;
		u32                 OldThreadId;
		i8             NewThreadPriority;
		i8             OldThreadPriority;
		u8               PreviousCState;
		i8                     SpareByte;
		i8           OldThreadWaitReason;
		i8             OldThreadWaitMode;
		i8                OldThreadState;
		i8   OldThreadWaitIdealProcessor;
		u32           NewThreadWaitTime;
		u32                    Reserved;
	};


	struct TraceTask : Thread {
		TraceTask(IAllocator& allocator);

		int task() override;
		static void callback(PEVENT_RECORD event);

		TRACEHANDLE open_handle;
	};
#else
	struct TraceTask {
		TraceTask(IAllocator&) {}
		void destroy() {}
		int open_handle;
	};
	void CloseTrace(int) {}
#endif

struct Instance {
	Instance(IAllocator& allocator)
		: tag_allocator(allocator, "profiler")
		, contexts(tag_allocator)
		, trace_task(tag_allocator)
		, counters(tag_allocator)
		, global_context(tag_allocator)
		, gpu_scopes(tag_allocator)
		, gpu_scope_stack(tag_allocator)
	{
		startTrace();
	}


	~Instance()
	{
		CloseTrace(trace_task.open_handle);
		trace_task.destroy();
		for (ThreadContext* ctx : contexts) {
			LUMIX_DELETE(tag_allocator, ctx);
		}
	}


	void startTrace()
	{
		#ifdef _WIN32
			if (CommandLineParser::isOn("-profile_cswitch")) {
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
					default: context_switches_enabled = false; break;
					case ERROR_SUCCESS: context_switches_enabled = true; break;
				}

				static EVENT_TRACE_LOGFILE trace = {};
				trace.LoggerName = (decltype(trace.LoggerName))KERNEL_LOGGER_NAME;
				trace.ProcessTraceMode = PROCESS_TRACE_MODE_RAW_TIMESTAMP | PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
				trace.EventRecordCallback = TraceTask::callback;
				trace_task.open_handle = OpenTrace(&trace);
				trace_task.create("profiler trace", true);
			}
		#endif
	}


	LUMIX_FORCE_INLINE ThreadContext* getThreadContext()
	{
		thread_local ThreadContext* ctx = [&](){
			ThreadContext* new_ctx = LUMIX_NEW(tag_allocator, ThreadContext)(tag_allocator);
			new_ctx->thread_id = os::getCurrentThreadID();
			MutexGuard lock(mutex);
			contexts.push(new_ctx);
			return new_ctx;
		}();

		return ctx;
	}

	TagAllocator tag_allocator;
	Array<u32> gpu_scope_stack;
	Array<GPUScope> gpu_scopes;
	Array<Counter> counters;
	Array<ThreadContext*> contexts;
	Mutex mutex;
	os::Timer timer;
	bool context_switches_enabled = false;
	u64 last_frame_duration = 0;
	u64 last_frame_time = 0;
	AtomicI32 fiber_wait_id = 0;
	TraceTask trace_task;
	ThreadContext global_context;
};

Local<Instance> g_instance;

// move data from temporary buffer to the main buffer
template <bool lock>
static void flush(ThreadContext& ctx) {
	if constexpr (lock) ctx.mutex.enter();

	if (ctx.num_pages < 500) {
		auto* new_page = LUMIX_NEW(ctx.allocator, ThreadContext::Page)();
		memcpy(new_page->buffer, ctx.tmp, ctx.tmp_pos);
		new_page->header.size = ctx.tmp_pos;
		if (!ctx.first_page) ctx.first_page = new_page;
		if (ctx.last_page) ctx.last_page->header.next = new_page;
		ctx.last_page = new_page;
		++ctx.num_pages;
	}
	else {
		ThreadContext::Page* new_page = ctx.first_page;
		ctx.first_page = new_page->header.next;
		new_page->header.next = nullptr;

		memcpy(new_page->buffer, ctx.tmp, ctx.tmp_pos);
		new_page->header.size = ctx.tmp_pos;

		ctx.last_page->header.next = new_page;
		ctx.last_page = new_page;
	}
	ctx.tmp_pos = 0;

	if constexpr (lock) ctx.mutex.exit();
}

template <bool lock, typename T>
LUMIX_FORCE_INLINE static void write(ThreadContext& ctx, u64 timestamp, EventType type, const T& value) {
	enum { num_bytes_to_write = sizeof(T) + sizeof(EventHeader) };
	ASSERT(num_bytes_to_write <= lengthOf(ctx.tmp));
	
	if constexpr (lock) ctx.mutex.enter();
	if (ctx.tmp_pos + num_bytes_to_write > lengthOf(ctx.tmp)) {
		flush<!lock>(ctx);
	}

	EventHeader* header = (EventHeader*)(ctx.tmp + ctx.tmp_pos);
	header->type = type;
	header->size = num_bytes_to_write;
	header->time = timestamp;
	memcpy((u8*)header + sizeof(*header), &value, sizeof(value));
	ctx.tmp_pos += num_bytes_to_write;
	if constexpr (lock) ctx.mutex.exit();
};

template <bool lock>
LUMIX_FORCE_INLINE static void write(ThreadContext& ctx, u64 timestamp, EventType type, Span<const u8> data) {
	const u32 num_bytes_to_write = (u32)data.length() + sizeof(EventHeader);
	ASSERT(num_bytes_to_write <= lengthOf(ctx.tmp));
	
	if constexpr (lock) ctx.mutex.enter();
	if (ctx.tmp_pos + num_bytes_to_write > lengthOf(ctx.tmp)) {
		flush<!lock>(ctx);
	}

	EventHeader* header = (EventHeader*)(ctx.tmp + ctx.tmp_pos);
	header->type = type;
	header->size = num_bytes_to_write;
	header->time = timestamp;
	memcpy((u8*)header + sizeof(*header), data.begin(), data.length());
	ctx.tmp_pos += num_bytes_to_write;
	if constexpr (lock) ctx.mutex.exit();
};

#ifdef _WIN32
	TraceTask::TraceTask(IAllocator& allocator)
		: Thread(allocator)
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
		write<true>(g_instance->global_context, rec.timestamp, profiler::EventType::CONTEXT_SWITCH, rec);
	};
#endif

u32 getCounterHandle(const char* key, float* last_value) {
	MutexGuard lock(g_instance->mutex);
	for (Counter& c : g_instance->counters) {
		if (equalStrings(c.name, key)) {
			if (last_value) *last_value = c.last_value;	
			return u32(&c - g_instance->counters.begin());
		}
	}
	return INVALID_COUNTER;
}

u32 createCounter(const char* key_literal, float min) {
	MutexGuard lock(g_instance->mutex);
	Counter& c = g_instance->counters.emplace();
	copyString(Span(c.name), key_literal);
	c.min = min;
	return g_instance->counters.size() - 1;
}

void pushCounter(u32 counter, float value) {
	{
		MutexGuard lock(g_instance->mutex);
		g_instance->counters[counter].last_value = value;
	}
	CounterRecord r;
	r.counter = counter;
	r.value = value;
	write<true>(g_instance->global_context, os::Timer::getRawTimestamp(), EventType::COUNTER, r);
}

void pushInt(const char* key, int value)
{
	ThreadContext* ctx = g_instance->getThreadContext();
	IntRecord r;
	r.key = key;
	r.value = value;
	write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::INT, r);
}


void pushString(const char* value)
{
	ThreadContext* ctx = g_instance->getThreadContext();
	write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::STRING, Span((const u8*)value, stringLength(value) + 1));
}

void blockColor(u32 abgr) {
	ThreadContext* ctx = g_instance->getThreadContext();
	write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::BLOCK_COLOR, abgr);
}

static AtomicI32 last_block_id = 0;

void beginJob(i32 signal_on_finish) {
	ThreadContext* ctx = g_instance->getThreadContext();

	JobRecord r;
	r.id = last_block_id.inc();
	r.signal_on_finish = signal_on_finish;

	if (ctx->open_block_stack_size < lengthOf(ctx->open_block_stack)) {
		ctx->open_block_stack[ctx->open_block_stack_size].id = r.id;
		ctx->open_block_stack[ctx->open_block_stack_size].name = "job";
	}
	++ctx->open_block_stack_size;

	write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::BEGIN_JOB, r);
}

u32 getOpenBlocks(Span<const char*> output) {
	ThreadContext* ctx = g_instance->getThreadContext();
	
	for (u32 i = 0; i < ctx->open_block_stack_size; ++i) {
		if (i >= output.length()) break;

		output[i] = ctx->open_block_stack[i].name;
	}

	return ctx->open_block_stack_size;
}

void beginBlock(const char* name) {
	BlockRecord r;
	r.id = last_block_id.inc();
	r.name = name;
	ThreadContext* ctx = g_instance->getThreadContext();

	if (ctx->open_block_stack_size < lengthOf(ctx->open_block_stack)) {
		ctx->open_block_stack[ctx->open_block_stack_size] = { r.id, name };
	}
	++ctx->open_block_stack_size;
	
	write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::BEGIN_BLOCK, r);
}

void endBlock()
{
	ThreadContext* ctx = g_instance->getThreadContext();
	if (ctx->open_block_stack_size > 0) {
		--ctx->open_block_stack_size;
		write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::END_BLOCK, 0);
	}
}

static GPUScope& getGPUScope(const char* name, u32& scope_id) {
	for (GPUScope& scope : g_instance->gpu_scopes) {
		if (scope.name == name) {
			scope_id = u32(&scope - g_instance->gpu_scopes.begin());
			return scope;
		}
	}

	GPUScope& scope = g_instance->gpu_scopes.emplace(g_instance->tag_allocator);
	scope.name = name;
	scope_id = g_instance->gpu_scopes.size() - 1;
	return scope;
}

void beginGPUBlock(const char* name, u64 timestamp, i64 profiler_link) {
	GPUBlock data;
	data.timestamp = timestamp;
	copyString(data.name, name);
	data.profiler_link = profiler_link;
	write<true>(g_instance->global_context, os::Timer::getRawTimestamp(), EventType::BEGIN_GPU_BLOCK, data);
	MutexGuard lock(g_instance->global_context.mutex);
	u32 scope_id;
	getGPUScope(name, scope_id).pushBegin(timestamp);
	g_instance->gpu_scope_stack.push(scope_id);
}

void gpuStats(u64 primitives_generated) {
	write<true>(g_instance->global_context, os::Timer::getRawTimestamp(), EventType::GPU_STATS, primitives_generated);
}

void endGPUBlock(u64 timestamp) {
	write<true>(g_instance->global_context, os::Timer::getRawTimestamp(), EventType::END_GPU_BLOCK, timestamp);
	MutexGuard lock(g_instance->global_context.mutex);

	if (g_instance->gpu_scope_stack.empty()) return;
	const u32 scope_id = g_instance->gpu_scope_stack.back();
	g_instance->gpu_scope_stack.pop();

	g_instance->gpu_scopes[scope_id].pushEnd(timestamp);
}

u32 getGPUScopeStats(Span<GPUScopeStats> out) {
	MutexGuard lock(g_instance->global_context.mutex);
	if (out.length() == 0) return g_instance->gpu_scopes.size();

	u32 num_outputs = minimum(out.length(), g_instance->gpu_scopes.size());
	double freq = (double)frequency();
	for (u32 i = 0; i < num_outputs; ++i) {
		const GPUScope& scope = g_instance->gpu_scopes[i];
		GPUScopeStats& stats = out[i];
		stats.name = scope.name.c_str();
		stats.min = scope.read == scope.write ? 0 : FLT_MAX;
		stats.max = 0;
		stats.avg = 0;
		const u32 len = lengthOf(scope.pairs);
		for (u32 j = scope.read; j < scope.write; ++j) {
			const float duration = float((scope.pairs[j % len].end - scope.pairs[j % len].begin) / freq);
			stats.min = minimum(stats.min, duration);
			stats.max = maximum(stats.max, duration);
			stats.avg += duration;
		}
		stats.avg /= float(scope.write - scope.read);
	}

	return num_outputs;
}

i64 createNewLinkID()
{
	static AtomicI64 counter = 1;
	return counter.inc();
}


void link(i64 link)
{
	ThreadContext* ctx = g_instance->getThreadContext();
	write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::LINK, link);
}


float getLastFrameDuration()
{
	return float(g_instance->last_frame_duration / double(frequency()));
}


void beforeFiberSwitch() {
	ThreadContext* ctx = g_instance->getThreadContext();
	const u64 now = os::Timer::getRawTimestamp();
	while(ctx->open_block_stack_size > 0) {
		write<false>(*ctx, now, EventType::END_BLOCK, 0);
		--ctx->open_block_stack_size;
	}
}


void signalTriggered(i32 job_system_signal) {
	ThreadContext* ctx = g_instance->getThreadContext();
	write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::SIGNAL_TRIGGERED, job_system_signal);
}

FiberSwitchData beginFiberWait(i32 job_system_signal) {
	FiberWaitRecord r;
	r.id = g_instance->fiber_wait_id.inc();
	r.job_system_signal = job_system_signal;

	FiberSwitchData res;

	ThreadContext* ctx = g_instance->getThreadContext();
	res.count = ctx->open_block_stack_size;
	res.id = r.id;
	res.signal = job_system_signal;
	const u32 num = minimum(res.count, lengthOf(res.blocks), lengthOf(ctx->open_block_stack));
	for (u32 i = 0; i < num; ++i) {
		res.blocks[i] = ctx->open_block_stack[i].id;
	}
	write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::BEGIN_FIBER_WAIT, r);
	return res;
}

void endFiberWait(const FiberSwitchData& switch_data) {
	ThreadContext* ctx = g_instance->getThreadContext();
	FiberWaitRecord r;
	r.id = switch_data.id;
	r.job_system_signal = switch_data.signal;

	const u64 now = os::Timer::getRawTimestamp();
	write<false>(*ctx, now, EventType::END_FIBER_WAIT, r);
	const u32 count = switch_data.count;
	
	for (u32 i = 0; i < count; ++i) {
		const i32 block_id = i < lengthOf(switch_data.blocks) ? switch_data.blocks[i] : -1;
		if (ctx->open_block_stack_size < lengthOf(ctx->open_block_stack)) {
			ctx->open_block_stack[ctx->open_block_stack_size] = {block_id, "N/A"};
		}
		++ctx->open_block_stack_size;
		write<false>(*ctx, now, EventType::CONTINUE_BLOCK, block_id);
	}
}


u64 frequency()
{
	return g_instance->timer.getFrequency();
}


bool contextSwitchesEnabled()
{
	return g_instance->context_switches_enabled;
}


void frame()
{
	const u64 n = os::Timer::getRawTimestamp();
	if (g_instance->last_frame_time != 0) {
		g_instance->last_frame_duration = n - g_instance->last_frame_time;
	}
	g_instance->last_frame_time = n;
	write<true>(g_instance->global_context, os::Timer::getRawTimestamp(), EventType::FRAME, 0);
}


void pushMutexEvent(u64 mutex_id, u64 begin_enter_time, u64 end_enter_time, u64 begin_exit_time, u64 end_exit_time) {
	ThreadContext* ctx = g_instance->getThreadContext();
	ASSERT(ctx);
	MutexEvent r = {
		.mutex_id = mutex_id,
		.begin_enter = begin_enter_time,
		.end_enter = end_enter_time,
		.begin_exit = begin_exit_time,
		.end_exit = end_exit_time
	};
	write<false>(*ctx, os::Timer::getRawTimestamp(), EventType::MUTEX_EVENT, r);
}

void showInProfiler(bool show)
{
	ThreadContext* ctx = g_instance->getThreadContext();
	MutexGuard lock(ctx->mutex);

	ctx->show_in_profiler = show;
}


void setThreadName(const char* name)
{
	ThreadContext* ctx = g_instance->getThreadContext();
	MutexGuard lock(ctx->mutex);

	ctx->thread_name = name;
}

static void saveStrings(OutputMemoryStream& blob) {
	HashMap<const char*, const char*> map(getGlobalAllocator());
	map.reserve(512);
	auto gather = [&](const ThreadContext& ctx){
		const ThreadContext::Page* page = ctx.first_page;
		while (page) {
			u32 iter = 0;
			while (iter < page->header.size) {
				profiler::EventHeader header;
				memcpy(&header, &page->buffer[iter], sizeof(header));
				
				switch (header.type) {
					case profiler::EventType::BEGIN_BLOCK: {
						BlockRecord b;
						memcpy(&b, &page->buffer[iter + sizeof(profiler::EventHeader)], sizeof(b));
						if (!map.find(b.name).isValid()) {
							map.insert(b.name, b.name);
						}
						break;
					}
					case profiler::EventType::INT: {
						IntRecord r;
						memcpy(&r, &page->buffer[iter + sizeof(profiler::EventHeader)], sizeof(r));
						if (!map.find(r.key).isValid()) {
							map.insert(r.key, r.key);
						}
						break;
					}
					default: break;
				}
				iter += header.size;
			}
			page = page->header.next;
		}
	};

	gather(g_instance->global_context);
	for (const ThreadContext* ctx : g_instance->contexts) {
		gather(*ctx);
	}

	blob.write(map.size());
	for (const char* iter : map) {
		blob.write((u64)(uintptr)iter);
		blob.write(iter, strlen(iter) + 1);
	}
}

void serialize(OutputMemoryStream& blob, ThreadContext& ctx) {
	MutexGuard lock(ctx.mutex);
	flush<false>(ctx);
	blob.writeString(ctx.thread_name);
	blob.write(ctx.thread_id);
	blob.write((u8)ctx.show_in_profiler);
	u32 size = 0;
	const ThreadContext::Page* page = ctx.first_page;
	while (page) {
		size += page->header.size;
		page = page->header.next;
	}

	blob.write(size);

	page = ctx.first_page;
	while (page) {
		blob.write(page->buffer, page->header.size);
		page = page->header.next;
	}
}

void serialize(OutputMemoryStream& blob) {
	MutexGuard lock(g_instance->mutex);
	
	const u32 version = 0;
	blob.write(version);
	blob.write((u32)g_instance->counters.size());
	blob.write(g_instance->counters.begin(), g_instance->counters.byte_size());

	blob.write((u32)g_instance->contexts.size());
	serialize(blob, g_instance->global_context);
	for (ThreadContext* ctx : g_instance->contexts) {
		serialize(blob, *ctx);
	}	
	saveStrings(blob);
}

void init(IAllocator& allocator) {
	g_instance.create(allocator);
}

void shutdown() {
	g_instance.destroy();
}

} // namespace Lumix


} //	namespace profiler
