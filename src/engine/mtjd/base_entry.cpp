#include "engine/mtjd/base_entry.h"
#include "engine/lumix.h"

#include "engine/mtjd/manager.h"

namespace Lumix
{
namespace MTJD
{


BaseEntry::BaseEntry(i32 depend_count, bool sync_event, IAllocator& allocator)
	: m_dependency_count(depend_count)
	, m_allocator(allocator)
	, m_dependency_table(m_allocator)
{
	m_sync_event = sync_event ? LUMIX_NEW(m_allocator, MT::Event)() : nullptr;
}


BaseEntry::~BaseEntry()
{
	LUMIX_DELETE(m_allocator, m_sync_event);
}


void BaseEntry::addDependency(BaseEntry* entry)
{
	m_dependency_table.push(entry);
	if (m_dependency_count > 0)
	{
		entry->incrementDependency();
	}
}


void BaseEntry::sync()
{
	ASSERT(nullptr != m_sync_event);
	m_sync_event->wait();
}


void BaseEntry::dependencyReady()
{
	DependencyTable dependency_table(m_dependency_table);
	m_dependency_table.clear();

	for (u32 i = 0, c = dependency_table.size(); c > i; ++i)
	{
		dependency_table[i]->decrementDependency();
	}

	if (m_sync_event)
	{
		m_sync_event->trigger();
	}
}


} // namepsace MTJD
} // namepsace Lumix
