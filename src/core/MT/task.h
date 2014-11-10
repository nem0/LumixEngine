#pragma once

#include "core/lumix.h"

namespace Lumix
{
	class IAllocator;

	namespace MT
	{
		class LUMIX_CORE_API Task
		{
		public:
			Task(IAllocator& allocator);
			~Task();

			virtual int task() = 0;

			bool create(const char* name);
			bool run();
			bool destroy();

			void setAffinityMask(uint32_t affinity_mask);
			void setPriority(uint32_t priority);

			uint32_t getAffinityMask() const;
			uint32_t getPriority() const;
			uint32_t getExitCode() const;

			bool isRunning() const;
			bool isFinished() const;
			bool isForceExit() const;

			void forceExit(bool wait);
			void exit(int32_t exit_code);

		protected:
			IAllocator& getAllocator();

		private:
			struct TaskImpl* m_implementation;
		};

		//namespace Logger
		//{
		//	struct Event
		//	{
		//		uint32_t tid;        // Thread ID
		//		const char* msg;  // Message string
		//		uint32_t param;      // A parameter which can mean anything you want
		//	};

		//	static const int BUFFER_SIZE = 65536;   // Must be a power of 2
		//	extern Event g_events[BUFFER_SIZE];
		//	extern LONG g_pos;

		//	inline void Log(const char* msg, uint32_t param)
		//	{
		//		// Get next event index
		//		LONG index = _InterlockedIncrement(&g_pos);
		//		// Write an event at this index
		//		Event* e = g_events + (index & (BUFFER_SIZE - 1));  // Wrap to buffer size
		//		e->tid = ((uint32_t*)__readfsdword(24))[9];           // Get thread ID
		//		e->msg = msg;
		//		e->param = param;
		//	}
		//}
	} // !namespace MT
} // !namespace Lumix
