#pragma once


#include "engine/lumix.h"
#include "engine/flag_set.h"
#include "engine/matrix.h"
#include "engine/iplugin.h"


struct lua_State;


namespace Lumix
{

struct AABB;
class Engine;
struct Frustum;
struct IAllocator;
class LIFOAllocator;
class Material;
struct Mesh;
class Model;
class Path;
struct Pose;
struct RayCastModelHit;
class Renderer;
class Shader;
class Terrain;
class Texture;
class Universe;
template <typename T> class Array;
template <typename T, typename T2> class AssociativeArray;
template <typename T> class DelegateList;


struct TerrainInfo
{
	Matrix m_world_matrix;
	Shader* m_shader;
	Terrain* m_terrain;
	Vec3 m_morph_const;
	float m_size;
	Vec3 m_min;
	int m_index;
};


struct DecalInfo
{
	Matrix mtx;
	Matrix inv_mtx;
	Material* material;
	Vec3 position;
	float radius;
};


struct ModelInstance
{
	enum Flags : u8
	{
		CUSTOM_MESHES = 1 << 0,
		KEEP_SKIN_DEPRECATED = 1 << 1,
		IS_BONE_ATTACHMENT_PARENT = 1 << 2,
		ENABLED = 1 << 3,

		RUNTIME_FLAGS = CUSTOM_MESHES,
		PERSISTENT_FLAGS = u8(~RUNTIME_FLAGS)
	};

	Matrix matrix;
	Model* model;
	Pose* pose;
	Entity entity;
	Mesh* meshes;
	FlagSet<Flags, u8> flags;
	i8 mesh_count;
};


struct MeshInstance
{
	Entity owner;
	Mesh* mesh;
	float depth;
};


struct GrassInfo
{
	struct InstanceData
	{
		Vec4 pos_scale;
		Quat rot;
		Vec4 normal;
	};
	Model* model;
	const InstanceData* instance_data;
	int instance_count;
	float type_distance;
};


struct DebugTriangle
{
	Vec3 p0;
	Vec3 p1;
	Vec3 p2;
	u32 color;
	float life;
};


struct DebugLine
{
	Vec3 from;
	Vec3 to;
	u32 color;
	float life;
};


struct DebugPoint
{
	Vec3 pos;
	u32 color;
	float life;
};


struct TextMeshVertex
{
	Vec3 pos;
	u32 color;
	Vec2 tex_coord;
};


class LUMIX_RENDERER_API RenderScene : public IScene
{
public:
	static RenderScene* createInstance(Renderer& renderer,
		Engine& engine,
		Universe& universe,
		IAllocator& allocator);
	static void destroyInstance(RenderScene* scene);
	static void registerLuaAPI(lua_State* L);

	virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, Entity ignore) = 0;
	virtual RayCastModelHit castRayTerrain(Entity entity, const Vec3& origin, const Vec3& dir) = 0;
	virtual void getRay(Entity entity, const Vec2& screen_pos, Vec3& origin, Vec3& dir) = 0;

	virtual float getCameraLODMultiplier(float fov, bool is_ortho) const = 0;
	virtual float getCameraLODMultiplier(Entity entity) const = 0;
	virtual Frustum getCameraFrustum(Entity entity) const = 0;
	virtual Frustum getCameraFrustum(Entity entity, const Vec2& a, const Vec2& b) const = 0;
	virtual float getTime() const = 0;
	virtual Engine& getEngine() const = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual Pose* lockPose(Entity entity) = 0;
	virtual void unlockPose(Entity entity, bool changed) = 0;
	virtual Entity getActiveGlobalLight() = 0;
	virtual void setActiveGlobalLight(Entity entity) = 0;
	virtual Vec4 getShadowmapCascades(Entity entity) = 0;
	virtual void setShadowmapCascades(Entity entity, const Vec4& value) = 0;

	virtual void addDebugTriangle(const Vec3& p0,
		const Vec3& p1,
		const Vec3& p2,
		u32 color,
		float life) = 0;
	virtual void addDebugPoint(const Vec3& pos, u32 color, float life) = 0;
	virtual void addDebugCone(const Vec3& vertex,
		const Vec3& dir,
		const Vec3& axis0,
		const Vec3& axis1,
		u32 color,
		float life) = 0;

	virtual void addDebugLine(const Vec3& from, const Vec3& to, u32 color, float life) = 0;
	virtual void addDebugCross(const Vec3& center, float size, u32 color, float life) = 0;
	virtual void addDebugCube(const Vec3& pos,
		const Vec3& dir,
		const Vec3& up,
		const Vec3& right,
		u32 color,
		float life) = 0;
	virtual void addDebugCube(const Vec3& from, const Vec3& max, u32 color, float life) = 0;
	virtual void addDebugCubeSolid(const Vec3& from, const Vec3& max, u32 color, float life) = 0;
	virtual void addDebugCircle(const Vec3& center,
		const Vec3& up,
		float radius,
		u32 color,
		float life) = 0;
	virtual void addDebugSphere(const Vec3& center, float radius, u32 color, float life) = 0;
	virtual void addDebugFrustum(const Frustum& frustum, u32 color, float life) = 0;

	virtual void addDebugCapsule(const Vec3& position,
		float height,
		float radius,
		u32 color,
		float life) = 0;

	virtual void addDebugCapsule(const Matrix& transform,
		float height,
		float radius,
		u32 color,
		float life) = 0;

	virtual void addDebugCylinder(const Vec3& position,
		const Vec3& up,
		float radius,
		u32 color,
		float life) = 0;

	virtual Entity getBoneAttachmentParent(Entity entity) = 0;
	virtual void setBoneAttachmentParent(Entity entity, Entity parent) = 0;
	virtual void setBoneAttachmentBone(Entity entity, int value) = 0;
	virtual int getBoneAttachmentBone(Entity entity) = 0;
	virtual Vec3 getBoneAttachmentPosition(Entity entity) = 0;
	virtual void setBoneAttachmentPosition(Entity entity, const Vec3& pos) = 0;
	virtual Vec3 getBoneAttachmentRotation(Entity entity) = 0;
	virtual void setBoneAttachmentRotation(Entity entity, const Vec3& rot) = 0;
	virtual void setBoneAttachmentRotationQuat(Entity entity, const Quat& rot) = 0;

	virtual const Array<DebugTriangle>& getDebugTriangles() const = 0;
	virtual const Array<DebugLine>& getDebugLines() const = 0;
	virtual const Array<DebugPoint>& getDebugPoints() const = 0;

	virtual Matrix getCameraProjection(Entity entity) = 0;
	virtual Matrix getCameraViewProjection(Entity entity) = 0;
	virtual Entity getCameraInSlot(const char* slot) const = 0;
	virtual float getCameraFOV(Entity entity) = 0;
	virtual void setCameraFOV(Entity entity, float fov) = 0;
	virtual void setCameraFarPlane(Entity entity, float far) = 0;
	virtual void setCameraNearPlane(Entity entity, float near) = 0;
	virtual float getCameraFarPlane(Entity entity) = 0;
	virtual float getCameraNearPlane(Entity entity) = 0;
	virtual float getCameraScreenWidth(Entity entity) = 0;
	virtual float getCameraScreenHeight(Entity entity) = 0;
	virtual void setCameraScreenSize(Entity entity, int w, int h) = 0;
	virtual bool isCameraOrtho(Entity entity) = 0;
	virtual void setCameraOrtho(Entity entity, bool is_ortho) = 0;
	virtual float getCameraOrthoSize(Entity entity) = 0;
	virtual void setCameraOrthoSize(Entity entity, float value) = 0;
	virtual Vec2 getCameraScreenSize(Entity entity) = 0;

	virtual void setScriptedParticleEmitterMaterialPath(Entity entity, const Path& path) = 0;
	virtual Path getScriptedParticleEmitterMaterialPath(Entity entity) = 0;
	virtual const AssociativeArray<Entity, class ScriptedParticleEmitter*>& getScriptedParticleEmitters() const = 0;

	virtual class ParticleEmitter* getParticleEmitter(Entity entity) = 0;
	virtual void resetParticleEmitter(Entity entity) = 0;
	virtual void updateEmitter(Entity entity, float time_delta) = 0;
	virtual const AssociativeArray<Entity, class ParticleEmitter*>& getParticleEmitters() const = 0;
	virtual const Vec2* getParticleEmitterAlpha(Entity entity) = 0;
	virtual int getParticleEmitterAlphaCount(Entity entity) = 0;
	virtual const Vec2* getParticleEmitterSize(Entity entity) = 0;
	virtual int getParticleEmitterSizeCount(Entity entity) = 0;
	virtual bool getParticleEmitterAutoemit(Entity entity) = 0;
	virtual bool getParticleEmitterLocalSpace(Entity entity) = 0;
	virtual Vec3 getParticleEmitterAcceleration(Entity entity) = 0;
	virtual Vec2 getParticleEmitterLinearMovementX(Entity entity) = 0;
	virtual Vec2 getParticleEmitterLinearMovementY(Entity entity) = 0;
	virtual Vec2 getParticleEmitterLinearMovementZ(Entity entity) = 0;
	virtual Vec2 getParticleEmitterInitialLife(Entity entity) = 0;
	virtual Int2 getParticleEmitterSpawnCount(Entity entity) = 0;
	virtual Vec2 getParticleEmitterSpawnPeriod(Entity entity) = 0;
	virtual Vec2 getParticleEmitterInitialSize(Entity entity) = 0;
	virtual void setParticleEmitterAutoemit(Entity entity, bool autoemit) = 0;
	virtual void setParticleEmitterLocalSpace(Entity entity, bool autoemit) = 0;
	virtual void setParticleEmitterAlpha(Entity entity, const Vec2* value, int count) = 0;
	virtual void setParticleEmitterSize(Entity entity, const Vec2* values, int count) = 0;
	virtual void setParticleEmitterAcceleration(Entity entity, const Vec3& value) = 0;
	virtual void setParticleEmitterLinearMovementX(Entity entity, const Vec2& value) = 0;
	virtual void setParticleEmitterLinearMovementY(Entity entity, const Vec2& value) = 0;
	virtual void setParticleEmitterLinearMovementZ(Entity entity, const Vec2& value) = 0;
	virtual void setParticleEmitterInitialLife(Entity entity, const Vec2& value) = 0;
	virtual void setParticleEmitterSpawnCount(Entity entity, const Int2& value) = 0;
	virtual void setParticleEmitterSpawnPeriod(Entity entity, const Vec2& value) = 0;
	virtual void setParticleEmitterInitialSize(Entity entity, const Vec2& value) = 0;
	virtual void setParticleEmitterMaterialPath(Entity entity, const Path& path) = 0;
	virtual void setParticleEmitterSubimageRows(Entity entity, const int& value) = 0;
	virtual void setParticleEmitterSubimageCols(Entity entity, const int& value) = 0;
	virtual Path getParticleEmitterMaterialPath(Entity entity) = 0;
	virtual int getParticleEmitterPlaneCount(Entity entity) = 0;
	virtual int getParticleEmitterSubimageRows(Entity entity) = 0;
	virtual int getParticleEmitterSubimageCols(Entity entity) = 0;
	virtual void addParticleEmitterPlane(Entity entity, int index) = 0;
	virtual void removeParticleEmitterPlane(Entity entity, int index) = 0;
	virtual Entity getParticleEmitterPlaneEntity(Entity entity, int index) = 0;
	virtual void setParticleEmitterPlaneEntity(Entity module, int index, Entity entity) = 0;
	virtual float getParticleEmitterPlaneBounce(Entity entity) = 0;
	virtual void setParticleEmitterPlaneBounce(Entity entity, float value) = 0;
	virtual float getParticleEmitterShapeRadius(Entity entity) = 0;
	virtual void setParticleEmitterShapeRadius(Entity entity, float value) = 0;

	virtual int getParticleEmitterAttractorCount(Entity entity) = 0;
	virtual void addParticleEmitterAttractor(Entity entity, int index) = 0;
	virtual void removeParticleEmitterAttractor(Entity entity, int index) = 0;
	virtual Entity getParticleEmitterAttractorEntity(Entity entity, int index) = 0;
	virtual void setParticleEmitterAttractorEntity(Entity module,
		int index,
		Entity entity) = 0;
	virtual float getParticleEmitterAttractorForce(Entity entity) = 0;
	virtual void setParticleEmitterAttractorForce(Entity entity, float value) = 0;

	virtual void enableModelInstance(Entity entity, bool enable) = 0;
	virtual bool isModelInstanceEnabled(Entity entity) = 0;
	virtual ModelInstance* getModelInstance(Entity entity) = 0;
	virtual ModelInstance* getModelInstances() = 0;
	virtual Path getModelInstancePath(Entity entity) = 0;
	virtual void setModelInstanceMaterial(Entity entity, int index, const Path& path) = 0;
	virtual Path getModelInstanceMaterial(Entity entity, int index) = 0;
	virtual int getModelInstanceMaterialsCount(Entity entity) = 0;
	virtual void setModelInstancePath(Entity entity, const Path& path) = 0;
	virtual Array<Array<MeshInstance>>& getModelInstanceInfos(const Frustum& frustum,
		const Vec3& lod_ref_point,
		float lod_multiplier,
		u64 layer_mask) = 0;
	virtual void getModelInstanceEntities(const Frustum& frustum, Array<Entity>& entities) = 0;
	virtual Entity getFirstModelInstance() = 0;
	virtual Entity getNextModelInstance(Entity entity) = 0;
	virtual Model* getModelInstanceModel(Entity entity) = 0;

	virtual void setDecalMaterialPath(Entity entity, const Path& path) = 0;
	virtual Path getDecalMaterialPath(Entity entity) = 0;
	virtual void setDecalScale(Entity entity, const Vec3& value) = 0;
	virtual Vec3 getDecalScale(Entity entity) = 0;
	virtual void getDecals(const Frustum& frustum, Array<DecalInfo>& decals) = 0;

	virtual void getGrassInfos(const Frustum& frustum,
		Entity entity,
		Array<GrassInfo>& infos) = 0;
	virtual void forceGrassUpdate(Entity entity) = 0;
	virtual void getTerrainInfos(const Frustum& frustum, const Vec3& lod_ref_point, Array<TerrainInfo>& infos) = 0;
	virtual float getTerrainHeightAt(Entity entity, float x, float z) = 0;
	virtual Vec3 getTerrainNormalAt(Entity entity, float x, float z) = 0;
	virtual void setTerrainMaterialPath(Entity entity, const Path& path) = 0;
	virtual Path getTerrainMaterialPath(Entity entity) = 0;
	virtual Material* getTerrainMaterial(Entity entity) = 0;
	virtual void setTerrainXZScale(Entity entity, float scale) = 0;
	virtual float getTerrainXZScale(Entity entity) = 0;
	virtual void setTerrainYScale(Entity entity, float scale) = 0;
	virtual float getTerrainYScale(Entity entity) = 0;
	virtual Vec2 getTerrainSize(Entity entity) = 0;
	virtual AABB getTerrainAABB(Entity entity) = 0;
	virtual Entity getTerrainEntity(Entity entity) = 0;
	virtual Vec2 getTerrainResolution(Entity entity) = 0;
	virtual Entity getFirstTerrain() = 0;
	virtual Entity getNextTerrain(Entity entity) = 0;

	virtual bool isGrassEnabled() const = 0;
	virtual int getGrassRotationMode(Entity entity, int index) = 0;
	virtual void setGrassRotationMode(Entity entity, int index, int value) = 0;
	virtual float getGrassDistance(Entity entity, int index) = 0;
	virtual void setGrassDistance(Entity entity, int index, float value) = 0;
	virtual void enableGrass(bool enabled) = 0;
	virtual void setGrassPath(Entity entity, int index, const Path& path) = 0;
	virtual Path getGrassPath(Entity entity, int index) = 0;
	virtual void setGrassDensity(Entity entity, int index, int density) = 0;
	virtual int getGrassDensity(Entity entity, int index) = 0;
	virtual int getGrassCount(Entity entity) = 0;
	virtual void addGrass(Entity entity, int index) = 0;
	virtual void removeGrass(Entity entity, int index) = 0;

	virtual int getClosestPointLights(const Vec3& pos, Entity* lights, int max_lights) = 0;
	virtual void getPointLights(const Frustum& frustum, Array<Entity>& lights) = 0;
	virtual void getPointLightInfluencedGeometry(Entity light,
		Entity camera,
		const Vec3& lod_ref_point, 
		Array<MeshInstance>& infos) = 0;
	virtual void getPointLightInfluencedGeometry(Entity light,
		Entity camera,
		const Vec3& lod_ref_point,
		const Frustum& frustum,
		Array<MeshInstance>& infos) = 0;
	virtual void setLightCastShadows(Entity entity, bool cast_shadows) = 0;
	virtual bool getLightCastShadows(Entity entity) = 0;
	virtual float getLightAttenuation(Entity entity) = 0;
	virtual void setLightAttenuation(Entity entity, float attenuation) = 0;
	virtual float getLightFOV(Entity entity) = 0;
	virtual void setLightFOV(Entity entity, float fov) = 0;
	virtual float getLightRange(Entity entity) = 0;
	virtual void setLightRange(Entity entity, float value) = 0;
	virtual void setPointLightIntensity(Entity entity, float intensity) = 0;
	virtual void setGlobalLightIntensity(Entity entity, float intensity) = 0;
	virtual void setGlobalLightIndirectIntensity(Entity entity, float intensity) = 0;
	virtual void setPointLightColor(Entity entity, const Vec3& color) = 0;
	virtual void setGlobalLightColor(Entity entity, const Vec3& color) = 0;
	virtual void setFogDensity(Entity entity, float density) = 0;
	virtual void setFogColor(Entity entity, const Vec3& color) = 0;
	virtual float getPointLightIntensity(Entity entity) = 0;
	virtual Entity getPointLightEntity(Entity entity) const = 0;
	virtual Entity getGlobalLightEntity(Entity entity) const = 0;
	virtual float getGlobalLightIntensity(Entity entity) = 0;
	virtual float getGlobalLightIndirectIntensity(Entity entity) = 0;
	virtual Vec3 getPointLightColor(Entity entity) = 0;
	virtual Vec3 getGlobalLightColor(Entity entity) = 0;
	virtual float getFogDensity(Entity entity) = 0;
	virtual float getFogBottom(Entity entity) = 0;
	virtual float getFogHeight(Entity entity) = 0;
	virtual void setFogBottom(Entity entity, float value) = 0;
	virtual void setFogHeight(Entity entity, float value) = 0;
	virtual Vec3 getFogColor(Entity entity) = 0;
	virtual Vec3 getPointLightSpecularColor(Entity entity) = 0;
	virtual void setPointLightSpecularColor(Entity entity, const Vec3& color) = 0;
	virtual float getPointLightSpecularIntensity(Entity entity) = 0;
	virtual void setPointLightSpecularIntensity(Entity entity, float color) = 0;

	virtual int getEnvironmentProbeIrradianceSize(Entity entity) = 0;
	virtual void setEnvironmentProbeIrradianceSize(Entity entity, int size) = 0;
	virtual int getEnvironmentProbeRadianceSize(Entity entity) = 0;
	virtual void setEnvironmentProbeRadianceSize(Entity entity, int size) = 0;
	virtual int getEnvironmentProbeReflectionSize(Entity entity) = 0;
	virtual void setEnvironmentProbeReflectionSize(Entity entity, int size) = 0;
	virtual bool isEnvironmentProbeReflectionEnabled(Entity entity) = 0;
	virtual void enableEnvironmentProbeReflection(Entity entity, bool enable) = 0;
	virtual bool isEnvironmentProbeCustomSize(Entity entity) = 0;
	virtual void enableEnvironmentProbeCustomSize(Entity entity, bool enable) = 0;
	virtual Texture* getEnvironmentProbeTexture(Entity entity) const = 0;
	virtual Texture* getEnvironmentProbeIrradiance(Entity entity) const = 0;
	virtual Texture* getEnvironmentProbeRadiance(Entity entity) const = 0;
	virtual void reloadEnvironmentProbe(Entity entity) = 0;
	virtual Entity getNearestEnvironmentProbe(const Vec3& pos) const = 0;
	virtual u64 getEnvironmentProbeGUID(Entity entity) const = 0;

	virtual void setTextMeshText(Entity entity, const char* text) = 0;
	virtual const char* getTextMeshText(Entity entity) = 0;
	virtual void setTextMeshFontSize(Entity entity, int value) = 0;
	virtual int getTextMeshFontSize(Entity entity) = 0;
	virtual Vec4 getTextMeshColorRGBA(Entity entity) = 0;
	virtual void setTextMeshColorRGBA(Entity entity, const Vec4& color) = 0;
	virtual Path getTextMeshFontPath(Entity entity) = 0;
	virtual void setTextMeshFontPath(Entity entity, const Path& path) = 0;
	virtual bool isTextMeshCameraOriented(Entity entity) = 0;
	virtual void setTextMeshCameraOriented(Entity entity, bool is_oriented) = 0;
	virtual void getTextMeshesVertices(Array<TextMeshVertex>& vertices, Entity camera) = 0;

protected:
	virtual ~RenderScene() {}
};
}