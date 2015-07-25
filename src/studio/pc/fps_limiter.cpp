#include "fps_limiter.h"

#include <Windows.h>
#include "core/iallocator.h"
#include "core/profiler.h"


class FPSLimiterPC : public FPSLimiter
{
	public:
		FPSLimiterPC(int fps, Lumix::IAllocator& allocator)
			: m_allocator(allocator)
		{
			m_fps = fps;
			m_timer = CreateWaitableTimer(nullptr, TRUE, "fps timer");
		}


		~FPSLimiterPC()
		{
			CloseHandle(m_timer);
		}


		virtual void beginFrame()
		{
			LARGE_INTEGER time;
			time.QuadPart = -10000000 / m_fps;
			SetWaitableTimer(m_timer, &time, 0, nullptr, nullptr, FALSE);
		}


		virtual void endFrame()
		{
			PROFILE_BLOCK("fps_limiter");
			WaitForSingleObject(m_timer, 1000 / m_fps);
		}


		Lumix::IAllocator& getAllocator() { return m_allocator; }

	private:
		Lumix::IAllocator& m_allocator;
		int m_fps;
		HANDLE m_timer;
};


FPSLimiter* FPSLimiter::create(int fps, Lumix::IAllocator& allocator)
{
	return allocator.newObject<FPSLimiterPC>(fps, allocator);
}


void FPSLimiter::destroy(FPSLimiter* limiter)
{
	static_cast<FPSLimiterPC*>(limiter)->getAllocator().deleteObject(limiter);
}

