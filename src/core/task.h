#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace MT
	{
		void sleep(uint32_t miliseconds);
		
		uint32_t getCurrentThreadID();
		uint32_t getProccessAffinityMask();
		bool isMainThread();
		void setMainThread();

		class LUX_CORE_API Task
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

			bool isRunning() const;
			bool isFinished() const;
			bool isForceExit() const;

			void forceExit(bool wait);

		private:
			struct TaskImpl* m_implementation;
		};	
	} // ~namespace MT
} // ~namespace Lux
