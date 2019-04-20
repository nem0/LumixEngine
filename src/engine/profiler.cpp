#include "engine/hash_map.h"
#include "engine/log.h"
#include "engine/timer.h"
#include "engine/mt/sync.h"
#include "engine/mt/thread.h"
#include "profiler.h"
#include <cstring>


namespace Lumix
{


namespace Profiler
{


static struct Instance
{
	Instance()
		: contexts(allocator)
		, timer(Timer::create(allocator))
	{}


	~Instance()
	{
		Timer::destroy(timer);
	}


	ThreadContext* getThreadContext()
	{
		thread_local ThreadContext* ctx = [&](){
			ThreadContext* new_ctx = LUMIX_NEW(allocator, ThreadContext)(allocator);
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
	u64 last_frame_duration = 0;
	u64 last_frame_time = 0;
} g_instance;


template <typename T>
void write(ThreadContext& ctx, EventType type, const T& value)
{
	if(g_instance.paused) return;

	#pragma pack(1)
	struct {
		EventHeader header;
		T value;
	} v;
	#pragma pack()
	v.header.type = type;
	v.header.size = sizeof(v);
	v.header.time = now();
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
	if(g_instance.paused) return;

	EventHeader header;
	header.type = type;
	ASSERT(sizeof(header) + size <= 0xffff);
	header.size = u16(sizeof(header) + size);
	header.time = now();

	MT::SpinLock lock(ctx.mutex);
	u8* buf = ctx.buffer.begin();
	const uint buf_size = ctx.buffer.size();

	while (header.size + ctx.end - ctx.begin > buf_size) {
		const u8 size = buf[ctx.begin % buf_size];
		ctx.begin += size;
	}

	auto cpy = [&](const u8* ptr, uint size){
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


void recordString(const char* value)
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
	++ctx->open_blocks_count;
	write(*ctx, EventType::BEGIN_BLOCK, name);
}


float getLastFrameDuration()
{
	return float(g_instance.last_frame_duration / double(frequency()));
}


void beginFiberSwitch()
{
	ThreadContext* ctx = g_instance.getThreadContext();
	while(ctx->open_blocks_count > 0) {
		write(*ctx, EventType::END_BLOCK, 0);
		--ctx->open_blocks_count;
	}
}


void endBlock()
{
	ThreadContext* ctx = g_instance.getThreadContext();
	if(ctx->open_blocks_count > 0) {
		--ctx->open_blocks_count;
		write(*ctx, EventType::END_BLOCK, 0);
	}
}


u64 now()
{
	return g_instance.timer->getRawTimeSinceStart();
}


u64 frequency()
{
	return g_instance.timer->getFrequency();
}


void frame()
{
	const u64 n = now();
	if (g_instance.last_frame_time != 0) {
		g_instance.last_frame_duration = n - g_instance.last_frame_time;
	}
	g_instance.last_frame_time = n;
	ThreadContext* ctx = g_instance.getThreadContext();
	write(*ctx, EventType::FRAME, 0);
}


void setThreadName(const char* name)
{
	ThreadContext* ctx = g_instance.getThreadContext();
	MT::SpinLock lock(ctx->mutex);

	ctx->name = name;
}


Array<ThreadContext*>& lockContexts()
{
	g_instance.mutex.lock();
	return g_instance.contexts;
}


void unlockContexts()
{
	g_instance.mutex.unlock();
}


void pause(bool paused)
{
	g_instance.paused = paused;
}


} // namespace Lumix


} //	namespace Profiler
