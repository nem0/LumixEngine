#pragma once


#include "engine/lumix.h"


namespace Lumix
{


using PrefabHandle = u32;

struct LUMIX_EDITOR_API PrefabSystem
{
	static PrefabSystem* create(struct WorldEditor& editor);
	static void destroy(PrefabSystem* system);
	static void createEditorPlugins(struct StudioApp& app, PrefabSystem& system);
	static void destroyEditorPlugins(StudioApp& app);

	virtual ~PrefabSystem() {}
	virtual void setUniverse(struct Universe*) = 0;
	virtual void update() = 0;
	virtual void serialize(struct OutputMemoryStream& serializer) = 0;
	virtual void deserialize(struct InputMemoryStream& serializer, const struct EntityMap& entity_map) = 0;
	virtual EntityPtr instantiatePrefab(struct PrefabResource& prefab, const struct DVec3& pos, const struct Quat& rot, float scale) = 0;
	virtual PrefabHandle getPrefab(EntityRef entity) const = 0;
	virtual void setPrefab(EntityRef entity, PrefabHandle prefab) = 0;
	virtual void savePrefab(const struct Path& path) = 0;
	virtual void breakPrefab(EntityRef e) = 0;
	virtual PrefabResource* getPrefabResource(EntityRef entity) = 0;
};


} // namespace Lumix