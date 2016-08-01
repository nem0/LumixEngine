#pragma once


#include "engine/lumix.h"
#include "engine/delegate_list.h"
#include "engine/string.h"


class StudioApp;


namespace Lumix
{


class InputBlob;
class OutputBlob;
class Path;
struct Quat;
struct Vec3;
class WorldEditor;


struct PrefabEntity
{
	uint32 path_hash;
	Entity prev;
	Entity next;
};


class LUMIX_EDITOR_API EntityTemplateSystem
{
public:
	static EntityTemplateSystem* create(WorldEditor& editor);
	static void destroy(EntityTemplateSystem* system);

	virtual ~EntityTemplateSystem() {}
	virtual void setStudioApp(StudioApp& app) = 0;
	virtual void serialize(OutputBlob& serializer) = 0;
	virtual void deserialize(InputBlob& serializer, bool has_prefabs) = 0;
	virtual void createTemplateFromEntity(const char* name, Entity entity) = 0;
	virtual uint32 getTemplate(Entity entity) = 0;
	virtual void setTemplate(Entity entity, uint32 template_name_hash) = 0;
	virtual const Array<Entity>& getInstances(uint32 template_name_hash) = 0;
	virtual Array<string>& getTemplateNames() = 0;
	virtual Entity createInstance(const char* name, const Vec3& position, const Quat& rot, float size) = 0;
	virtual void refreshPrefabs() = 0;

	virtual bool isPrefab() = 0;
	virtual void applyPrefab() = 0;
	virtual void selectPrefab() = 0;
	virtual void savePrefab(const Lumix::Path& path) = 0;
	virtual void instantiatePrefab(const Vec3& pos, const Lumix::Path& path) = 0;
	virtual PrefabEntity getPrefabEntity(Entity entity) = 0;
	virtual void setPrefab(Entity entity, const PrefabEntity& prefab) = 0;

	virtual DelegateList<void()>& updated() = 0;
};


} // namespace Lumix