#pragma once


#include "engine/lumix.h"


namespace Lumix
{


struct DVec3;
struct IInputStream;
struct IOutputStream;
struct Path;
struct PrefabResource;
struct Quat;
struct Vec3;
struct WorldEditor;
struct StudioApp;

using PrefabHandle = u32;

struct LUMIX_EDITOR_API PrefabSystem
{
	static PrefabSystem* create(WorldEditor& editor);
	static void destroy(PrefabSystem* system);
	static void createEditorPlugins(StudioApp& app, PrefabSystem& system);
	static void destroyEditorPlugins(StudioApp& app);

	virtual ~PrefabSystem() {}
	virtual void update() = 0;
	virtual void serialize(IOutputStream& serializer) = 0;
	virtual void deserialize(IInputStream& serializer, const struct EntityMap& entity_map) = 0;
	virtual EntityPtr instantiatePrefab(PrefabResource& prefab, const DVec3& pos, const Quat& rot, float scale) = 0;
	virtual PrefabHandle getPrefab(EntityRef entity) const = 0;
	virtual void setPrefab(EntityRef entity, PrefabHandle prefab) = 0;
	virtual void savePrefab(const Path& path) = 0;
	virtual PrefabResource* getPrefabResource(EntityRef entity) = 0;
};


} // namespace Lumix