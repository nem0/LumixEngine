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


void beginBlock(const char* name, u32 color)
{
	ThreadContext* ctx = g_instance.getThreadContext();
	struct {
		const char* name;
		u32 color;
	} v{name, color};
	++ctx->open_blocks_count;
	write(*ctx, EventType::BEGIN_BLOCK, v);
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
