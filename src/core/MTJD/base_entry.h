#pragma once

#include "core/MT/task.h"
#include "core/MT/event.h"
#include "core/Array.h"

namespace Lumix
{
	namespace MTJD
	{
		class LUMIX_CORE_API BaseEntry abstract
		{
		public:
			typedef Array<BaseEntry*> DependencyTable;

			BaseEntry(int32_t depend_count, bool sync_event);
			virtual ~BaseEntry();

			void addDependency(BaseEntry* entry);

			void sync();

			virtual void incrementDependency() = 0;
			virtual void decrementDependency() = 0;

			uint32_t getDependenceCount() const { return m_dependency_count; }

		protected:

			void dependencyReady();

			MT::Event*			m_sync_event;
			volatile int32_t	m_dependency_count;
			DependencyTable		m_dependency_table;
		};
	} // ~namepsace MTJD
} // ~namepsace Lumix
