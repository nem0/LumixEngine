#include "core/lumix.h"
#include "core/MTJD/base_entry.h"

#include "core/MTJD/manager.h"

namespace Lumix
{
	namespace MTJD
	{
		BaseEntry::BaseEntry(int32_t depend_count, bool sync_event, IAllocator& allocator)
			: m_dependency_count(depend_count)
			, m_allocator(allocator)
		{
#if TYPE == MULTI_THREAD

			m_sync_event = sync_event ? m_allocator.newObject<MT::Event>(MT::EventFlags::MANUAL_RESET) : NULL;

#endif // TYPE == MULTI_THREAD
		}

		BaseEntry::~BaseEntry()
		{
#if TYPE == MULTI_THREAD

			m_allocator.deleteObject(m_sync_event);

#endif //TYPE == MULTI_THREAD
		}

		void BaseEntry::addDependency(BaseEntry* entry)
		{
#if TYPE == MULTI_THREAD

			m_dependency_table.push(entry);
			if (m_dependency_count > 0)
			{
				entry->incrementDependency();
			}

#endif //TYPE == MULTI_THREAD
		}

		void BaseEntry::sync()
		{
#if TYPE == MULTI_THREAD

			ASSERT(NULL != m_sync_event);
			m_sync_event->wait();

#endif //TYPE == MULTI_THREAD
		}

		void BaseEntry::dependencyReady()
		{
#if TYPE == MULTI_THREAD
			DependencyTable dependency_table(m_dependency_table);
			m_dependency_table.clear();

			for (uint32_t i = 0, c = dependency_table.size(); c > i; ++i)
			{
				dependency_table[i]->decrementDependency();
			}

			if (m_sync_event)
			{
				m_sync_event->trigger();
			}

#endif // TYPE == MULTI_THREAD
		}
	} // ~namepsace MTJD
} // ~namepsace Lumix