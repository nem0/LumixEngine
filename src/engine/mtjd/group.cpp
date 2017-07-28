#include "engine/mtjd/group.h"
#include "engine/lumix.h"
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
	m_static_dependency_table.push(entry);
	if (m_dependency_count > 0)
	{
		entry->incrementDependency();
	}
}


void Group::incrementDependency()
{
	i32 count = MT::atomicIncrement(&m_dependency_count);
	if (1 == count)
	{
		dependencyNotReady();
	}
}


void Group::decrementDependency()
{
	i32 count = MT::atomicDecrement(&m_dependency_count);
	if (0 == count)
	{
		dependencyReady();
	}

	ASSERT(0 <= count);
}


void Group::dependencyNotReady()
{
	for (u32 i = 0, c = m_static_dependency_table.size(); c > i; ++i)
	{
		m_static_dependency_table[i]->incrementDependency();
	}

	for (u32 i = 0, c = m_dependency_table.size(); c > i; ++i)
	{
		m_dependency_table[i]->incrementDependency();
	}

	if (m_sync_event)
	{
		m_sync_event->reset();
	}
}


void Group::dependencyReady()
{
	BaseEntry::dependencyReady();

	for (u32 i = 0, c = m_static_dependency_table.size(); c > i; ++i)
	{
		m_static_dependency_table[i]->decrementDependency();
	}
}


} // namepsace MTJD
} // namepsace Lumix
