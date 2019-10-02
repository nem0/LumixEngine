#pragma once


#include "engine/lumix.h"


namespace Lumix
{


struct DVec3;
struct IDeserializer;
struct IInputStream;
struct IOutputStream;
struct ISerializer;
class Path;
struct PrefabResource;
struct Quat;
struct Vec3;
class WorldEditor;
class StudioApp;


class LUMIX_EDITOR_API PrefabSystem
{
public:
	static PrefabSystem* create(WorldEditor& editor);
	static void destroy(PrefabSystem* system);
	static void createEditorPlugins(StudioApp& app, PrefabSystem& system);
	static void destroyEditorPlugins(StudioApp& app);

	virtual ~PrefabSystem() {}
	virtual void update() = 0;
	virtual void serialize(IOutputStream& serializer) = 0;
	virtual void deserialize(IInputStream& serializer) = 0;
	virtual void serialize(ISerializer& serializer) = 0;
	virtual void deserialize(IDeserializer& serializer) = 0;
	virtual EntityPtr instantiatePrefab(PrefabResource& prefab, const DVec3& pos, const Quat& rot, float scale) = 0;
	virtual u64 getPrefab(EntityRef entity) const = 0;
	virtual int getMaxEntityIndex() const = 0;
	virtual void setPrefab(EntityRef entity, u64 prefab) = 0;
	virtual EntityPtr getFirstInstance(u64 prefab) = 0;
	virtual EntityPtr getNextInstance(EntityRef entity) = 0;
	virtual void savePrefab(const Path& path) = 0;
	virtual PrefabResource* getPrefabResource(EntityRef entity) = 0;
};


} // namespace Lumix