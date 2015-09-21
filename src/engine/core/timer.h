#pragma once


namespace Lumix
{
	class LUMIX_ENGINE_API Timer
	{
		public:
			virtual ~Timer() {}

			/// returns time (seconds) since the last tick() call or since the creation of the timer
			virtual float tick() = 0;
			virtual float getTimeSinceStart() = 0;
			virtual float getTimeSinceTick() = 0;

			static Timer* create(IAllocator& allocator);
			static void destroy(Timer* timer);
	};

	class ScopedTimer
	{
	public:
		ScopedTimer(const char* name, IAllocator& allocator)
			: m_name(name)
			, m_timer(Timer::create(allocator))
		{

		}

		~ScopedTimer()
		{
			Timer::destroy(m_timer);
		}

		float getTimeSinceStart()
		{
			return m_timer->getTimeSinceStart();
		}

		const char* getName() const { return m_name; }

	private:
		const char* m_name;
		Timer* m_timer;
	};
} // !namespace Lumix
