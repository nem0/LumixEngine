#pragma once

#include "engine/lumix.hpp"

#include "core/hash.hpp"
#include "core/hash_map.hpp"



namespace Lumix
{


using PrefabHandle = FilePathHash;
template <typename T> struct UniquePtr;
enum class WorldVersion : u32;

struct LUMIX_EDITOR_API PrefabSystem
{
	static UniquePtr<PrefabSystem> create(struct WorldEditor& editor);
	static void createEditorPlugins(struct StudioApp& app, PrefabSystem& system);
	static void destroyEditorPlugins(StudioApp& app);

	virtual ~PrefabSystem() {}
	virtual void setWorld(struct World*) = 0;
	virtual void update() = 0;
	virtual void serialize(struct OutputMemoryStream& serializer) = 0;
	virtual void deserialize(struct InputMemoryStream& serializer, const struct EntityMap& entity_map, WorldVersion version) = 0;
	virtual EntityPtr instantiatePrefab(struct PrefabResource& prefab, const struct DVec3& pos, const struct Quat& rot, const struct Vec3& scale) = 0;
	virtual void instantiatePrefabs(struct PrefabResource& prefab, Span<struct Transform> transforms) = 0;
	virtual PrefabHandle getPrefab(EntityRef entity) const = 0;
	virtual void setPrefab(EntityRef entity, PrefabHandle prefab) = 0;
	virtual void savePrefab(EntityRef entity, const struct Path& path) = 0;
	virtual void breakPrefab(EntityRef e) = 0;
	virtual PrefabResource* getPrefabResource(EntityRef entity) = 0;
	virtual void cloneTo(PrefabSystem& dst, const HashMap<EntityPtr, EntityPtr>& map) = 0;
};


} // namespace Lumix