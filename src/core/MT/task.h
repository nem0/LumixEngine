#pragma once

#include "core/lumix.h"

namespace Lumix
{
	namespace MT
	{
		LUMIX_CORE_API void sleep(uint32_t miliseconds);
		LUMIX_CORE_API inline void yield() { sleep(0); }

		LUMIX_CORE_API uint32_t getCPUsCount();
		
		LUMIX_CORE_API uint32_t getCurrentThreadID();
		LUMIX_CORE_API uint32_t getProccessAffinityMask();

		LUMIX_CORE_API bool isMainThread();
		LUMIX_CORE_API void setMainThread();

		class LUMIX_CORE_API Task
		{
		public:
			Task();
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
			void exit(int32_t exitCode);

		private:
			struct TaskImpl* m_implementation;
		};	
	} // ~namespace MT
} // ~namespace Lumix
