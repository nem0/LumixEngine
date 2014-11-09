#include "core/lumix.h"
#include "core/MTJD/group.h"
#include "core/mt/atomic.h"

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
#if TYPE == MULTI_THREAD

			m_static_dependency_table.push(entry);
			if (m_dependency_count > 0)
			{
				entry->incrementDependency();
			}

#endif //TYPE == MULTI_THREAD
		}

		void Group::incrementDependency()
		{
#if TYPE == MULTI_THREAD

			int32_t count = MT::atomicIncrement(&m_dependency_count);
			if (1 == count)
			{
				dependencyNotReady();
			}

#endif //TYPE == MULTI_THREAD
		}

		void Group::decrementDependency()
		{
#if TYPE == MULTI_THREAD

			int32_t count = MT::atomicDecrement(&m_dependency_count);
			if (0 == count)
			{
				dependencyReady();
			}

			ASSERT(0 <= count);

#endif //TYPE == MULTI_THREAD
		}

		void Group::dependencyNotReady()
		{
#if TYPE == MULTI_THREAD

			for (uint32_t i = 0, c = m_static_dependency_table.size(); c > i; ++i)
			{
				m_static_dependency_table[i]->incrementDependency();
			}

			for (uint32_t i = 0, c = m_dependency_table.size(); c > i; ++i)
			{
				m_dependency_table[i]->incrementDependency();
			}

			if (m_sync_event)
			{
				m_sync_event->reset();
			}

#endif //TYPE == MULTI_THREAD
		}

		void Group::dependencyReady()
		{
#if TYPE == MULTI_THREAD

			BaseEntry::dependencyReady();

			for (uint32_t i = 0, c = m_static_dependency_table.size(); c > i; ++i)
			{
				m_static_dependency_table[i]->decrementDependency();
			}

#endif //TYPE == MULTI_THREAD
		}
	} // ~namepsace MTJD
} // ~namepsace Lumix
