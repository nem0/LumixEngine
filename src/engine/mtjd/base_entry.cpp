#include "engine/lumix.h"
#include "engine/mtjd/base_entry.h"

#include "engine/mtjd/manager.h"

namespace Lumix
{
	namespace MTJD
	{
		BaseEntry::BaseEntry(int32 depend_count, bool sync_event, IAllocator& allocator)
			: m_dependency_count(depend_count)
			, m_allocator(allocator)
			, m_dependency_table(m_allocator)
		{
#if !LUMIX_SINGLE_THREAD()

			m_sync_event = sync_event
							   ? LUMIX_NEW(m_allocator, MT::Event)()
							   : nullptr;

#endif
		}

		BaseEntry::~BaseEntry()
		{
#if !LUMIX_SINGLE_THREAD()

			LUMIX_DELETE(m_allocator, m_sync_event);

#endif
		}

		void BaseEntry::addDependency(BaseEntry* entry)
		{
#if !LUMIX_SINGLE_THREAD()

			m_dependency_table.push(entry);
			if (m_dependency_count > 0)
			{
				entry->incrementDependency();
			}

#endif
		}

		void BaseEntry::sync()
		{
#if !LUMIX_SINGLE_THREAD()

			ASSERT(nullptr != m_sync_event);
			m_sync_event->wait();

#endif
		}

		void BaseEntry::dependencyReady()
		{
#if !LUMIX_SINGLE_THREAD()
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

#endif
		}
	} // namepsace MTJD
} // namepsace Lumix
