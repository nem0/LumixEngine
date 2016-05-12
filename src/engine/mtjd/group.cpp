#include "engine/lumix.h"
#include "engine/mtjd/group.h"
#include "engine/mt/atomic.h"
#include "engine/mt/sync.h"

namespace Lumix
{
	namespace MTJD
	{
		Group::Group(bool sync_event, IAllocator& allocator)
			: BaseEntry(0, sync_event, allocator)
			, m_static_dependency_table(allocator)
		{
		}

		Group::~Group()
		{
		}

		void Group::addStaticDependency(BaseEntry* entry)
		{
#if !LUMIX_SINGLE_THREAD()

			m_static_dependency_table.push(entry);
			if (m_dependency_count > 0)
			{
				entry->incrementDependency();
			}

#endif
		}

		void Group::incrementDependency()
		{
#if !LUMIX_SINGLE_THREAD()

			int32 count = MT::atomicIncrement(&m_dependency_count);
			if (1 == count)
			{
				dependencyNotReady();
			}

#endif
		}

		void Group::decrementDependency()
		{
#if !LUMIX_SINGLE_THREAD()

			int32 count = MT::atomicDecrement(&m_dependency_count);
			if (0 == count)
			{
				dependencyReady();
			}

			ASSERT(0 <= count);

#endif
		}

		void Group::dependencyNotReady()
		{
#if !LUMIX_SINGLE_THREAD()

			for (uint32 i = 0, c = m_static_dependency_table.size(); c > i; ++i)
			{
				m_static_dependency_table[i]->incrementDependency();
			}

			for (uint32 i = 0, c = m_dependency_table.size(); c > i; ++i)
			{
				m_dependency_table[i]->incrementDependency();
			}

			if (m_sync_event)
			{
				m_sync_event->reset();
			}

#endif
		}

		void Group::dependencyReady()
		{
#if !LUMIX_SINGLE_THREAD()

			BaseEntry::dependencyReady();

			for (uint32 i = 0, c = m_static_dependency_table.size(); c > i; ++i)
			{
				m_static_dependency_table[i]->decrementDependency();
			}

#endif
		}
	} // namepsace MTJD
} // namepsace Lumix
