#include "lumix.h"
#include "core/MTJD/base_entry.h"

#include "core/MTJD/manager.h"

namespace Lumix
{
	namespace MTJD
	{
		BaseEntry::BaseEntry(int32 depend_count, bool sync_event, IAllocator& allocator)
			: m_dependency_count(depend_count)
			, m_allocator(allocator)
			, m_dependency_table(m_allocator)
		{
#if TYPE == MULTI_THREAD

			m_sync_event = sync_event
							   ? LUMIX_NEW(m_allocator, MT::Event)((int)MT::EventFlags::MANUAL_RESET)
							   : nullptr;

#endif // TYPE == MULTI_THREAD
		}

		BaseEntry::~BaseEntry()
		{
#if TYPE == MULTI_THREAD

			LUMIX_DELETE(m_allocator, m_sync_event);

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

			ASSERT(nullptr != m_sync_event);
			m_sync_event->wait();

#endif //TYPE == MULTI_THREAD
		}

		void BaseEntry::dependencyReady()
		{
#if TYPE == MULTI_THREAD
			DependencyTable dependency_table(m_dependency_table);
			m_dependency_table.clear();

			for (uint32 i = 0, c = dependency_table.size(); c > i; ++i)
			{
				dependency_table[i]->decrementDependency();
			}

			if (m_sync_event)
			{
				m_sync_event->trigger();
			}

#endif // TYPE == MULTI_THREAD
		}
	} // namepsace MTJD
} // namepsace Lumix