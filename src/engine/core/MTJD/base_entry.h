#pragma once


#include "core/Array.h"


namespace Lumix
{


namespace MT
{


class Event;


}


namespace MTJD
{


class LUMIX_ENGINE_API BaseEntry
{
public:
	typedef Array<BaseEntry*> DependencyTable;

	BaseEntry(int32 depend_count, bool sync_event, IAllocator& allocator);
	virtual ~BaseEntry();

	void addDependency(BaseEntry* entry);

	void sync();

	virtual void incrementDependency() = 0;
	virtual void decrementDependency() = 0;

	uint32 getDependenceCount() const { return m_dependency_count; }

protected:
	void dependencyReady();

	IAllocator& m_allocator;
	MT::Event* m_sync_event;
	volatile int32 m_dependency_count;
	DependencyTable m_dependency_table;
};


} // namepsace MTJD
} // namepsace Lumix
