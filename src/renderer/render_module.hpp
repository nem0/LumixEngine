#pragma once

#include "engine/lumix.hpp"

#include "core/color.hpp"
#include "core/array.hpp"
#include "core/geometry.hpp"
#include "core/hash_map.hpp"
#include "core/math.hpp"
#include "core/stream.hpp"

#include "engine/plugin.hpp"
#include "gpu/gpu.hpp"


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
struct World;
template <typename T> struct Array;
template <typename T> struct Delegate;
template <typename T, typename T2> struct AssociativeArray;

struct ProceduralGeometry {
	ProceduralGeometry(IAllocator& allocator) 
		: vertex_data(allocator)
		, index_data(allocator)
		, vertex_decl(gpu::PrimitiveType::TRIANGLES)
	{}

	Material* material = nullptr;
	OutputMemoryStream vertex_data;
	OutputMemoryStream index_data;
	gpu::VertexDecl vertex_decl;
	gpu::DataType index_type;
	gpu::BufferHandle vertex_buffer = gpu::INVALID_BUFFER;
	gpu::BufferHandle index_buffer = gpu::INVALID_BUFFER;
	AABB aabb;
	
	u32 getVertexCount() const;
	u32 getIndexCount() const;
};

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

struct Decal {
	Material* material = nullptr;
	Transform transform;
	float radius;
	EntityRef entity;
	EntityPtr prev_decal = INVALID_ENTITY;
	EntityPtr next_decal = INVALID_ENTITY;
	Vec3 half_extents;
	Vec2 uv_scale;
};

struct CurveDecal {
	Material* material = nullptr;
	Transform transform;
	float radius;
	EntityRef entity;
	EntityPtr prev_decal = INVALID_ENTITY;
	EntityPtr next_decal = INVALID_ENTITY;
	Vec3 half_extents;
	Vec2 uv_scale;
	Vec2 bezier_p0;
	Vec2 bezier_p2;
};

struct Environment {
	enum Flags : u32 {
		NONE = 0,
		CAST_SHADOWS = 1 << 0
	};

	Vec3 light_color;
	float direct_intensity;
	float indirect_intensity;
	EntityRef entity;
	Vec4 cascades;
	Flags flags = Flags::NONE;
};

struct PointLight {
	enum Flags : u32 {
		NONE = 0,
		CAST_SHADOWS = 1 << 0,
		DYNAMIC = 1 << 1
	};

	Vec3 color;
	float intensity;
	EntityRef entity;
	float fov;
	float attenuation_param;
	float range;
	Flags flags = Flags::NONE;
	u64 guid;
};

struct ReflectionProbe {
	enum Flags {
		NONE = 0,
		ENABLED = 1 << 2,
	};

	u64 guid;
	Flags flags = Flags::NONE;
	u32 size = 128;
	Vec3 half_extents = Vec3(100, 100, 100);
	u32 texture_id = 0xffFFffFF;

	struct LoadJob;
	LoadJob* load_job = nullptr;
};

struct EnvironmentProbe {
	enum Flags {
		NONE = 0,
		ENABLED = 1 << 2,
	};

	Vec3 inner_range;
	Vec3 outer_range;
	Flags flags = Flags::NONE;
	Vec3 sh_coefs[9];
};

struct ModelInstance {
	enum Flags : u8 {
		NONE = 0,
		IS_BONE_ATTACHMENT_PARENT = 1 << 0,
		ENABLED = 1 << 1,
		VALID = 1 << 2,
		MOVED = 1 << 3,
	};

	Model* model;
	Mesh* meshes;
	Pose* pose;
	Material* custom_material = nullptr; 
	EntityPtr next_model = INVALID_ENTITY;
	EntityPtr prev_model = INVALID_ENTITY;
	float lod = 4;
	Transform prev_frame_transform;
	Flags flags = Flags::NONE;
	u16 mesh_count;
};

struct InstancedModel {
	InstancedModel(IAllocator& allocator) 
		: instances(allocator)
	{}

	struct InstanceData {
		Vec3 rot_quat;
		float lod;
		Vec3 pos;
		float scale;
	};

	struct Grid {
		struct Cell {
			AABB aabb;
			u32 from_instance;
			u32 instance_count;
		};

		AABB aabb;
		Cell cells[4 * 4];
	};

	Grid grid;
	Model* model = nullptr;
	Array<InstanceData> instances;
	gpu::BufferHandle gpu_data = gpu::INVALID_BUFFER;
	u32 gpu_capacity = 0;
	bool dirty = false;
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
	Color color;
};


struct DebugLine
{
	DVec3 from;
	DVec3 to;
	Color color;
};


enum class RenderableTypes : u8 {
	MESH,
	SKINNED,
	DECAL,
	LOCAL_LIGHT,
	MESH_MATERIAL_OVERRIDE,
	FUR,
	CURVE_DECAL,
	PARTICLES,

	COUNT
};


struct FurComponent {
	u32 layers = 16;
	float scale = 0.01f;
	float gravity = 1.f;
	bool enabled = true;
};

enum class RenderModuleVersion : i32 {
	DECAL_UV_SCALE,
	CURVE_DECALS,
	AUTODESTROY_EMITTER,
	SMALLER_MODEL_INSTANCES,
	INSTANCED_MODEL,
	SPLINES,
	SPLINES_VERTEX_COLORS,
	PROCEDURAL_GEOMETRY_PRIMITIVE_TYPE,
	PROCEDURAL_GEOMETRY_INDEX_BUFFER,
	TESSELATED_TERRAIN,
	REMOVED_SPLINE_GEOMETRY,
	EMIT_RATE_REMOVED,

	LATEST
};


struct LUMIX_RENDERER_API RenderModule : IModule
{
	static UniquePtr<RenderModule> createInstance(Renderer& renderer,
		Engine& engine,
		World& world,
		IAllocator& allocator);
	static void registerLuaAPI(lua_State* L, Renderer& renderer);
	static void reflect();

	virtual RayCastModelHit castRay(const Ray& ray, const Delegate<bool (const RayCastModelHit&)> filter) = 0;
	virtual RayCastModelHit castRay(const Ray& ray, EntityPtr ignore) = 0;
	virtual RayCastModelHit castRayTerrain(const Ray& ray) = 0;
	virtual RayCastModelHit castRayInstancedModels(const Ray& ray, const Delegate<bool (const RayCastModelHit&)>& filter) = 0;
	virtual struct Ray getCameraRay(EntityRef entity, const Vec2& screen_pos) = 0;

	virtual void setActiveCamera(EntityRef camera) = 0;
	virtual EntityPtr getActiveCamera() const = 0;
	virtual	struct Viewport getCameraViewport(EntityRef camera) const = 0;
	virtual float getCameraLODMultiplier(float fov, bool is_ortho) const = 0;
	virtual float getCameraLODMultiplier(EntityRef entity) const = 0;
	virtual ShiftedFrustum getCameraFrustum(EntityRef entity) const = 0;
	virtual ShiftedFrustum getCameraFrustum(EntityRef entity, const Vec2& a, const Vec2& b) const = 0;
	virtual Engine& getEngine() const = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual Pose* lockPose(EntityRef entity) = 0;
	virtual void unlockPose(EntityRef entity, bool changed) = 0;
	virtual EntityPtr getActiveEnvironment() = 0;
	virtual void setActiveEnvironment(EntityRef entity) = 0;
	virtual Vec4 getShadowmapCascades(EntityRef entity) = 0;
	virtual void setShadowmapCascades(EntityRef entity, const Vec4& value) = 0;

	virtual DebugTriangle* addDebugTriangles(int count) = 0;
	virtual void addDebugTriangle(const DVec3& p0, const DVec3& p1, const DVec3& p2, Color color) = 0;
	virtual void addDebugLine(const DVec3& from, const DVec3& to, Color color) = 0; 
	virtual DebugLine* addDebugLines(int count) = 0;
	virtual void addDebugCross(const DVec3& center, float size, Color color) = 0;
	virtual void addDebugBone(const DVec3& pos, const Vec3& dir, const Vec3& up, const Vec3& right, Color color) = 0;
	virtual void addDebugCube(const DVec3& pos, const Vec3& dir, const Vec3& up, const Vec3& right, Color color) = 0;
	virtual void addDebugCube(const DVec3& from, const DVec3& max, Color color) = 0;
	virtual void addDebugCubeSolid(const DVec3& from, const DVec3& max, Color color) = 0;

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

	virtual void setParticleSystemPath(EntityRef entity, const Path& path) = 0;
	virtual Path getParticleSystemPath(EntityRef entity) = 0;
	virtual void updateParticleSystem(EntityRef entity, float dt) = 0;
	virtual const HashMap<EntityRef, struct ParticleSystem>& getParticleSystems() const = 0;
	virtual ParticleSystem& getParticleSystem(EntityRef e) = 0;

	virtual Path getInstancedModelPath(EntityRef entity) = 0;
	virtual void setInstancedModelPath(EntityRef entity, const Path& path) = 0;
	virtual const HashMap<EntityRef, InstancedModel>& getInstancedModels() const = 0;
	virtual InstancedModel& beginInstancedModelEditing(EntityRef entity) = 0;
	virtual void endInstancedModelEditing(EntityRef entity) = 0;
	virtual void initInstancedModelGPUData(EntityRef entity) = 0;

	virtual void enableModelInstance(EntityRef entity, bool enable) = 0;
	virtual bool isModelInstanceEnabled(EntityRef entity) = 0;
	virtual ModelInstance* getModelInstance(EntityRef entity) = 0;
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

	virtual CurveDecal& getCurveDecal(EntityRef entity) = 0;
	virtual void setCurveDecalMaterialPath(EntityRef entity, const Path& path) = 0;
	virtual Path getCurveDecalMaterialPath(EntityRef entity) = 0;
	virtual void setCurveDecalHalfExtents(EntityRef entity, float value) = 0;
	virtual float getCurveDecalHalfExtents(EntityRef entity) = 0;
	virtual void setCurveDecalUVScale(EntityRef entity, const Vec2& value) = 0;
	virtual Vec2 getCurveDecalUVScale(EntityRef entity) = 0;
	virtual void setCurveDecalBezierP0(EntityRef entity, const Vec2& value) = 0;
	virtual Vec2 getCurveDecalBezierP0(EntityRef entity) = 0;
	virtual void setCurveDecalBezierP2(EntityRef entity, const Vec2& value) = 0;
	virtual Vec2 getCurveDecalBezierP2(EntityRef entity) = 0;

	virtual Decal& getDecal(EntityRef entity) = 0;
	virtual void setDecalMaterialPath(EntityRef entity, const Path& path) = 0;
	virtual Path getDecalMaterialPath(EntityRef entity) = 0;
	virtual void setDecalHalfExtents(EntityRef entity, const Vec3& value) = 0;
	virtual Vec3 getDecalHalfExtents(EntityRef entity) = 0;

	virtual Terrain* getTerrain(EntityRef entity) = 0;
	virtual const HashMap<EntityRef, Terrain*>& getTerrains() = 0;
	virtual float getTerrainHeightAt(EntityRef entity, float x, float z) = 0;
	virtual Vec3 getTerrainNormalAt(EntityRef entity, float x, float z) = 0;
	virtual void setTerrainMaterialPath(EntityRef entity, const Path& path) = 0;
	virtual Path getTerrainMaterialPath(EntityRef entity) = 0;
	virtual Material* getTerrainMaterial(EntityRef entity) = 0;
	virtual void setTerrainXZScale(EntityRef entity, float scale) = 0;
	virtual float getTerrainXZScale(EntityRef entity) = 0;
	virtual void setTerrainTesselation(EntityRef entity, u32 value) = 0;
	virtual u32 getTerrainTesselation(EntityRef entity) = 0;
	virtual void setTerrainBaseGridResolution(EntityRef entity, u32 value) = 0;
	virtual u32 getTerrainBaseGridResolution(EntityRef entity) = 0;
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
	virtual void setGrassSpacing(EntityRef entity, int index, float spacing) = 0;
	virtual float getGrassSpacing(EntityRef entity, int index) = 0;
	virtual int getGrassCount(EntityRef entity) = 0;
	virtual void addGrass(EntityRef entity, int index) = 0;
	virtual void removeGrass(EntityRef entity, int index) = 0;

	virtual void setProceduralGeometry(EntityRef entity
		, Span<const u8> vertex_data
		, const gpu::VertexDecl& vertex_decl
		, Span<const u8> index_data
		, gpu::DataType index_type) = 0;
	virtual void setProceduralGeometryMaterial(EntityRef entity, const Path& path) = 0;
	virtual Path getProceduralGeometryMaterial(EntityRef entity) = 0;
	virtual const HashMap<EntityRef, ProceduralGeometry>& getProceduralGeometries() = 0;
	virtual ProceduralGeometry& getProceduralGeometry(EntityRef e) = 0;

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
	virtual gpu::TextureHandle getReflectionProbesTexture() = 0;
	virtual void reloadReflectionProbes() = 0;

	virtual Span<EntityRef> getEnvironmentProbesEntities() = 0;
	virtual EnvironmentProbe& getEnvironmentProbe(EntityRef entity) = 0;
	virtual void enableEnvironmentProbe(EntityRef entity, bool enable) = 0;
	virtual bool isEnvironmentProbeEnabled(EntityRef entity) = 0;
	virtual Span<const EnvironmentProbe> getEnvironmentProbes() = 0;
};

}