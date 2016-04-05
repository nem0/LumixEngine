#pragma once

#include "lumix.h"

namespace Lumix
{
	class IAllocator;

	namespace MT
	{
		class LUMIX_ENGINE_API Task
		{
		public:
			Task(IAllocator& allocator);
			virtual ~Task();

			virtual int task() = 0;

			bool create(const char* name);
			bool run();
			bool destroy();

			void setAffinityMask(uint32 affinity_mask);
			void setPriority(uint32 priority);

			uint32 getAffinityMask() const;
			uint32 getPriority() const;
			uint32 getExitCode() const;

			bool isRunning() const;
			bool isFinished() const;
			bool isForceExit() const;

			void forceExit(bool wait);
			void exit(int32 exit_code);

		protected:
			IAllocator& getAllocator();

		private:
			struct TaskImpl* m_implementation;
		};

	} // !namespace MT
} // !namespace Lumix
