#pragma once


#include "lumix.h"
#include "core/array.h"


namespace Lumix
{


class InputBlob;
class OutputBlob;
class Universe;


class LUMIX_EDITOR_API EntityGroups
{
public:
	EntityGroups(IAllocator& allocator);

	void setGroup(Entity entity, int group);
	const Entity* getGroupEntities(int idx) const;

	void createGroup(const char* name);
	void deleteGroup(int idx);
	int getGroup(const char* name) const;
	int getGroupCount() const;
	int getGroupEntitiesCount(int idx) const;
	const char* getGroupName(int idx) const;
	void setGroupName(int idx, const char* name);

	void setUniverse(Universe* universe);
	void allEntitiesToDefault();

	void serialize(OutputBlob& blob);
	void deserialize(InputBlob& blob);

private:
	struct GroupName
	{
		char name[20];
	};

private:
	void removeFromGroup(Entity entity);
	void onEntityCreated(Entity entity);
	void onEntityDestroyed(Entity entity);

private:
	IAllocator& m_allocator;
	Array<Array<Entity> > m_groups;
	Array<GroupName> m_group_names;
	Universe* m_universe;
};


} // namespace Lumix
