#pragma once

#include "engine/lumix.h"

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
			bool destroy();

			void setAffinityMask(uint32 affinity_mask);

			uint32 getAffinityMask() const;

			bool isRunning() const;
			bool isFinished() const;
			bool isForceExit() const;

			void forceExit(bool wait);

		protected:
			IAllocator& getAllocator();

		private:
			struct TaskImpl* m_implementation;
		};

	} // !namespace MT
} // !namespace Lumix
