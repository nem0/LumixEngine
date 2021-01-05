#pragma once


#include "engine/lumix.h"
#include "engine/flag_set.h"
#include "engine/hash_map.h"
#include "engine/math.h"
#include "engine/plugin.h"
#include "gpu/gpu.h"


struct lua_State;


namespace Lumix
{


struct AABB;
struct CullResult;
struct Engine;
struct Frustum;
struct IAllocator;
struct Material;
struct Mesh;
struct Model;
struct Path;
struct Pose;
struct RayCastModelHit;
struct Renderer;
struct Shader;
struct ShiftedFrustum;
struct Terrain;
struct Texture;
struct Universe;
template <typename T> struct Array;
template <typename T, typename T2> struct AssociativeArray;


struct Camera
{
	EntityRef entity;
	float fov;
	float near;
	float far;
	float ortho_size;
	float screen_width;
	float screen_height;
	bool is_ortho;
};


struct TerrainInfo
{
	DVec3 position;
	Quat rot;
	Shader* shader;
	Terrain* terrain;
	Vec3 min;
	int index;
};

struct Environment
{
	enum Flags : u32 {
		CAST_SHADOWS = 1 << 0
	};

	Vec3 diffuse_color;
	float diffuse_intensity;
	float indirect_intensity;
	EntityRef entity;
	Vec4 cascades;
	FlagSet<Flags, u32> flags;
};


struct PointLight
{
	enum Flags : u32 {
		CAST_SHADOWS = 1 << 0,
		DYNAMIC = 1 << 1
	};

	Vec3 color;
	float intensity;
	EntityRef entity;
	float fov;
	float attenuation_param;
	float range;
	FlagSet<Flags, u32> flags;
	u64 guid;
};

struct ReflectionProbe
{
	enum Flags {
		ENABLED = 1 << 2,
	};

	Texture* texture = nullptr;
	u64 guid;
	FlagSet<Flags, u32> flags;
	u32 size = 128;
	Vec3 half_extents = Vec3(100, 100, 100);
};

struct EnvironmentProbe
{
	enum Flags {
		ENABLED = 1 << 2,
	};

	Vec3 inner_range;
	Vec3 outer_range;
	FlagSet<Flags, u32> flags;
	Vec3 sh_coefs[9];
};


struct MeshSortData
{
    u32 sort_key;
    u8 layer;
};


struct ModelInstance
{
	enum Flags : u8
	{
		IS_BONE_ATTACHMENT_PARENT = 1 << 0,
		ENABLED = 1 << 1,
		VALID = 1 << 2,
	};

	Model* model;
	Mesh* meshes;
	Pose* pose;
	Material* custom_material = nullptr; 
	EntityPtr next_model = INVALID_ENTITY;
	EntityPtr prev_model = INVALID_ENTITY;
	float lod = 4;
	FlagSet<Flags, u8> flags;
	u16 mesh_count;
};


struct MeshInstance
{
	EntityRef owner;
	const Mesh* mesh;
	float depth;
};


struct EnvProbeInfo
{
	DVec3 position;
	Vec3 half_extents;
	gpu::TextureHandle reflection;
	gpu::TextureHandle radiance;
	Vec3 sh_coefs[9];
	bool use_irradiance;
};


struct DebugTriangle
{
	DVec3 p0;
	DVec3 p1;
	DVec3 p2;
	u32 color;
};


struct DebugLine
{
	DVec3 from;
	DVec3 to;
	u32 color;
};


enum class RenderableTypes : u8 {
	MESH_GROUP,
	MESH,
	SKINNED,
	DECAL,
	LOCAL_LIGHT,
	MESH_MATERIAL_OVERRIDE,
	FUR,

	COUNT
};


struct FurComponent {
	u32 layers = 16;
	float scale = 0.01f;
	float gravity = 1.f;
	bool enabled = true;
};

struct LUMIX_RENDERER_API RenderScene : IScene
{
	static UniquePtr<RenderScene> createInstance(Renderer& renderer,
		Engine& engine,
		Universe& universe,
		IAllocator& allocator);
	static void registerLuaAPI(lua_State* L);
	static void reflect();

	virtual RayCastModelHit castRay(const DVec3& origin, const Vec3& dir, EntityPtr ignore) = 0;
	virtual RayCastModelHit castRayTerrain(EntityRef entity, const DVec3& origin, const Vec3& dir) = 0;
	virtual void getRay(EntityRef entity, const Vec2& screen_pos, DVec3& origin, Vec3& dir) = 0;

	virtual EntityPtr getActiveCamera() const = 0;
	virtual	struct Viewport getCameraViewport(EntityRef camera) const = 0;
	virtual float getCameraLODMultiplier(float fov, bool is_ortho) const = 0;
	virtual float getCameraLODMultiplier(EntityRef entity) const = 0;
	virtual ShiftedFrustum getCameraFrustum(EntityRef entity) const = 0;
	virtual ShiftedFrustum getCameraFrustum(EntityRef entity, const Vec2& a, const Vec2& b) const = 0;
	virtual float getTime() const = 0;
	virtual Engine& getEngine() const = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual void setGlobalLODMultiplier(float multiplier) = 0;
	virtual float getGlobalLODMultiplier() const = 0;

	virtual Pose* lockPose(EntityRef entity) = 0;
	virtual void unlockPose(EntityRef entity, bool changed) = 0;
	virtual EntityPtr getActiveEnvironment() = 0;
	virtual void setActiveEnvironment(EntityRef entity) = 0;
	virtual Vec4 getShadowmapCascades(EntityRef entity) = 0;
	virtual void setShadowmapCascades(EntityRef entity, const Vec4& value) = 0;

	virtual DebugTriangle* addDebugTriangles(int count) = 0;
	virtual void addDebugTriangle(const DVec3& p0, const DVec3& p1, const DVec3& p2, u32 color) = 0;
	virtual void addDebugLine(const DVec3& from, const DVec3& to, u32 color) = 0; 
	virtual DebugLine* addDebugLines(int count) = 0;
	virtual void addDebugCross(const DVec3& center, float size, u32 color) = 0;
	virtual void addDebugCube(const DVec3& pos, const Vec3& dir, const Vec3& up, const Vec3& right, u32 color) = 0;
	virtual void addDebugCube(const DVec3& from, const DVec3& max, u32 color) = 0;
	virtual void addDebugCubeSolid(const DVec3& from, const DVec3& max, u32 color) = 0;

	virtual EntityPtr getBoneAttachmentParent(EntityRef entity) = 0;
	virtual void setBoneAttachmentParent(EntityRef entity, EntityPtr parent) = 0;
	virtual void setBoneAttachmentBone(EntityRef entity, int value) = 0;
	virtual int getBoneAttachmentBone(EntityRef entity) = 0;
	virtual Vec3 getBoneAttachmentPosition(EntityRef entity) = 0;
	virtual void setBoneAttachmentPosition(EntityRef entity, const Vec3& pos) = 0;
	virtual Vec3 getBoneAttachmentRotation(EntityRef entity) = 0;
	virtual void setBoneAttachmentRotation(EntityRef entity, const Vec3& rot) = 0;
	virtual void setBoneAttachmentRotationQuat(EntityRef entity, const Quat& rot) = 0;

	virtual HashMap<EntityRef, FurComponent>& getFurs() = 0;
	virtual FurComponent& getFur(EntityRef e) = 0;

	virtual void clearDebugLines() = 0;
	virtual void clearDebugTriangles() = 0;
	virtual const Array<DebugTriangle>& getDebugTriangles() const = 0;
	virtual const Array<DebugLine>& getDebugLines() const = 0;

	virtual Camera& getCamera(EntityRef entity) = 0;
	virtual Matrix getCameraProjection(EntityRef entity) = 0;
	virtual float getCameraScreenWidth(EntityRef entity) = 0;
	virtual float getCameraScreenHeight(EntityRef entity) = 0;
	virtual void setCameraScreenSize(EntityRef entity, int w, int h) = 0;
	virtual Vec2 getCameraScreenSize(EntityRef entity) = 0;

	virtual void setParticleEmitterPath(EntityRef entity, const Path& path) = 0;
	virtual Path getParticleEmitterPath(EntityRef entity) = 0;
	virtual void setParticleEmitterRate(EntityRef entity, u32 value) = 0;
	virtual u32 getParticleEmitterRate(EntityRef entity) = 0;
	virtual void updateParticleEmitter(EntityRef entity, float dt) = 0;
	virtual const AssociativeArray<EntityRef, struct ParticleEmitter*>& getParticleEmitters() const = 0;

	virtual void enableModelInstance(EntityRef entity, bool enable) = 0;
	virtual bool isModelInstanceEnabled(EntityRef entity) = 0;
	virtual ModelInstance* getModelInstance(EntityRef entity) = 0;
	virtual const MeshSortData* getMeshSortData() const = 0;
	virtual Span<const ModelInstance> getModelInstances() const = 0;
	virtual Span<ModelInstance> getModelInstances() = 0;
	virtual Path getModelInstancePath(EntityRef entity) = 0;
	virtual void setModelInstanceLOD(EntityRef entity, u32 lod) = 0;
	virtual void setModelInstancePath(EntityRef entity, const Path& path) = 0;
	virtual void setModelInstanceMaterialOverride(EntityRef entity, const Path& path) = 0;
	virtual Path getModelInstanceMaterialOverride(EntityRef entity) = 0;
	virtual CullResult* getRenderables(const ShiftedFrustum& frustum, RenderableTypes type) const = 0;
	virtual CullResult* getRenderables(const ShiftedFrustum& frustum) const = 0;
	virtual EntityPtr getFirstModelInstance() = 0;
	virtual EntityPtr getNextModelInstance(EntityPtr entity) = 0;
	virtual Model* getModelInstanceModel(EntityRef entity) = 0;

	virtual void setDecalMaterialPath(EntityRef entity, const Path& path) = 0;
	virtual Path getDecalMaterialPath(EntityRef entity) = 0;
	virtual void setDecalHalfExtents(EntityRef entity, const Vec3& value) = 0;
	virtual Vec3 getDecalHalfExtents(EntityRef entity) = 0;
	virtual Material* getDecalMaterial(EntityRef entity) const = 0;

	virtual Terrain* getTerrain(EntityRef entity) = 0;
	virtual const HashMap<EntityRef, Terrain*>& getTerrains() = 0;
	virtual void getTerrainInfos(Array<TerrainInfo>& infos) = 0;
	virtual float getTerrainHeightAt(EntityRef entity, float x, float z) = 0;
	virtual Vec3 getTerrainNormalAt(EntityRef entity, float x, float z) = 0;
	virtual void setTerrainMaterialPath(EntityRef entity, const Path& path) = 0;
	virtual Path getTerrainMaterialPath(EntityRef entity) = 0;
	virtual Material* getTerrainMaterial(EntityRef entity) = 0;
	virtual void setTerrainXZScale(EntityRef entity, float scale) = 0;
	virtual float getTerrainXZScale(EntityRef entity) = 0;
	virtual void setTerrainYScale(EntityRef entity, float scale) = 0;
	virtual float getTerrainYScale(EntityRef entity) = 0;
	virtual Vec2 getTerrainSize(EntityRef entity) = 0;
	virtual AABB getTerrainAABB(EntityRef entity) = 0;
	virtual IVec2 getTerrainResolution(EntityRef entity) = 0;
	virtual EntityPtr getFirstTerrain() = 0;
	virtual EntityPtr getNextTerrain(EntityRef entity) = 0;

	virtual int getGrassRotationMode(EntityRef entity, int index) = 0;
	virtual void setGrassRotationMode(EntityRef entity, int index, int value) = 0;
	virtual float getGrassDistance(EntityRef entity, int index) = 0;
	virtual void setGrassDistance(EntityRef entity, int index, float value) = 0;
	virtual void setGrassPath(EntityRef entity, int index, const Path& path) = 0;
	virtual Path getGrassPath(EntityRef entity, int index) = 0;
	virtual void setGrassDensity(EntityRef entity, int index, int density) = 0;
	virtual int getGrassDensity(EntityRef entity, int index) = 0;
	virtual int getGrassCount(EntityRef entity) = 0;
	virtual void addGrass(EntityRef entity, int index) = 0;
	virtual void removeGrass(EntityRef entity, int index) = 0;

	virtual bool getEnvironmentCastShadows(EntityRef entity) = 0;
	virtual void setEnvironmentCastShadows(EntityRef entity, bool enable) = 0;
	virtual Environment& getEnvironment(EntityRef entity) = 0;
	virtual const HashMap<EntityRef, PointLight>& getPointLights() = 0;
	virtual PointLight& getPointLight(EntityRef entity) = 0;
	virtual float getLightRange(EntityRef entity) = 0;
	virtual void setLightRange(EntityRef entity, float value) = 0;
	virtual bool getPointLightCastShadows(EntityRef entity) = 0;
	virtual void setPointLightCastShadows(EntityRef entity, bool value) = 0;
	virtual bool getPointLightDynamic(EntityRef entity) = 0;
	virtual void setPointLightDynamic(EntityRef entity, bool value) = 0;

	virtual Span<EntityRef> getReflectionProbesEntities() = 0;
	virtual ReflectionProbe& getReflectionProbe(EntityRef entity) = 0;
	virtual void enableReflectionProbe(EntityRef entity, bool enable) = 0;
	virtual bool isReflectionProbeEnabled(EntityRef entity) = 0;
	virtual Span<const ReflectionProbe> getReflectionProbes() = 0;

	virtual Span<EntityRef> getEnvironmentProbesEntities() = 0;
	virtual EnvironmentProbe& getEnvironmentProbe(EntityRef entity) = 0;
	virtual void enableEnvironmentProbe(EntityRef entity, bool enable) = 0;
	virtual bool isEnvironmentProbeEnabled(EntityRef entity) = 0;
	virtual Span<const EnvironmentProbe> getEnvironmentProbes() = 0;
};

}