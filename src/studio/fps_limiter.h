#pragma once


namespace Lumix
{
	class IAllocator;
}


class FPSLimiter
{
	public:
		static void destroy(FPSLimiter* limiter);
		static FPSLimiter* create(int fps, Lumix::IAllocator& allocator);

		virtual ~FPSLimiter() {}

		virtual void beginFrame() = 0;
		virtual void endFrame() = 0;
};