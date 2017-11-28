#pragma once


#include "engine/lumix.h"


namespace Lumix
{


struct IDeserializer;
class InputBlob;
struct ISerializer;
class OutputBlob;
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

	virtual ~PrefabSystem() {}
	virtual void setStudioApp(StudioApp& app) = 0;
	virtual void serialize(OutputBlob& serializer) = 0;
	virtual void deserialize(InputBlob& serializer) = 0;
	virtual void serialize(ISerializer& serializer) = 0;
	virtual void deserialize(IDeserializer& serializer) = 0;
	virtual Entity instantiatePrefab(PrefabResource& prefab, const Vec3& pos, const Quat& rot, float scale) = 0;
	virtual u64 getPrefab(Entity entity) const = 0;
	virtual int getMaxEntityIndex() const = 0;
	virtual void setPrefab(Entity entity, u64 prefab) = 0;
	virtual Entity getFirstInstance(u64 prefab) = 0;
	virtual Entity getNextInstance(Entity entity) = 0;
	virtual void savePrefab(const Path& path) = 0;
	virtual PrefabResource* getPrefabResource(Entity entity) = 0;
};


} // namespace Lumix