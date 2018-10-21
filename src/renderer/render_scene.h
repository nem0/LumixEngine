#pragma once


#include "engine/lumix.h"
#include "engine/flag_set.h"
#include "engine/matrix.h"
#include "engine/iplugin.h"
#include "ffr/ffr.h"


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
struct ShiftedFrustum;
class Terrain;
class Texture;
class Universe;
template <typename T> class Array;
template <typename T, typename T2> class AssociativeArray;
template <typename T> class DelegateList;


struct TerrainInfo
{
	DVec3 position;
	Quat rot;
	Shader* shader;
	Terrain* terrain;
	Vec3 morph_const;
	float size;
	Vec3 min;
	int index;
};


struct DecalInfo
{
	Material* material;
	Transform transform;
	float radius;
};


struct ModelInstance
{
	enum Flags : u8
	{
		IS_BONE_ATTACHMENT_PARENT = 1 << 0,
		ENABLED = 1 << 1,
	};

	Model* model;
	Pose* pose;
	EntityPtr entity;
	EntityPtr next_model = INVALID_ENTITY;
	EntityPtr prev_model = INVALID_ENTITY;
	Mesh* meshes;
	FlagSet<Flags, u8> flags;
	u8 mesh_count;
};


struct MeshInstance
{
	EntityRef owner;
	const Mesh* mesh;
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


struct EnvProbeInfo
{
	DVec3 position;
	float radius;
	ffr::TextureHandle reflection;
	ffr::TextureHandle radiance;
	ffr::TextureHandle irradiance;
};


struct DebugTriangle
{
	DVec3 p0;
	DVec3 p1;
	DVec3 p2;
	u32 color;
	float life;
};


struct DebugLine
{
	DVec3 from;
	DVec3 to;
	u32 color;
	float life;
};


struct DebugPoint
{
	DVec3 pos;
	u32 color;
	float life;
};

enum class RenderableTypes : u8 {
	MESH_GROUP,
	MESH,
	SKINNED,
	DECAL,
	LOCAL_LIGHT,

	COUNT
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

	virtual Pose* lockPose(EntityRef entity) = 0;
	virtual void unlockPose(EntityRef entity, bool changed) = 0;
	virtual EntityPtr getActiveGlobalLight() = 0;
	virtual void setActiveGlobalLight(EntityRef entity) = 0;
	virtual Vec4 getShadowmapCascades(EntityRef entity) = 0;
	virtual void setShadowmapCascades(EntityRef entity, const Vec4& value) = 0;

	virtual void addDebugTriangle(const DVec3& p0,
		const DVec3& p1,
		const DVec3& p2,
		u32 color,
		float life) = 0;
	virtual void addDebugPoint(const DVec3& pos, u32 color, float life) = 0;
	virtual void addDebugCone(const DVec3& vertex,
		const Vec3& dir,
		const Vec3& axis0,
		const Vec3& axis1,
		u32 color,
		float life) = 0;

	virtual void addDebugLine(const DVec3& from, const DVec3& to, u32 color, float life) = 0;
	virtual void addDebugCross(const DVec3& center, float size, u32 color, float life) = 0;
	virtual void addDebugCube(const DVec3& pos,
		const Vec3& dir,
		const Vec3& up,
		const Vec3& right,
		u32 color,
		float life) = 0;
	virtual void addDebugCube(const DVec3& from, const DVec3& max, u32 color, float life) = 0;
	virtual void addDebugCubeSolid(const DVec3& from, const DVec3& max, u32 color, float life) = 0;
	virtual void addDebugCircle(const DVec3& center,
		const Vec3& up,
		float radius,
		u32 color,
		float life) = 0;
	virtual void addDebugSphere(const DVec3& center, float radius, u32 color, float life) = 0;
	virtual void addDebugFrustum(const ShiftedFrustum& frustum, u32 color, float life) = 0;

	virtual void addDebugCapsule(const DVec3& position,
		float height,
		float radius,
		u32 color,
		float life) = 0;

	virtual void addDebugCapsule(const Matrix& transform,
		float height,
		float radius,
		u32 color,
		float life) = 0;

	virtual void addDebugCylinder(const DVec3& position,
		const Vec3& up,
		float radius,
		u32 color,
		float life) = 0;

	virtual EntityPtr getBoneAttachmentParent(EntityRef entity) = 0;
	virtual void setBoneAttachmentParent(EntityRef entity, EntityPtr parent) = 0;
	virtual void setBoneAttachmentBone(EntityRef entity, int value) = 0;
	virtual int getBoneAttachmentBone(EntityRef entity) = 0;
	virtual Vec3 getBoneAttachmentPosition(EntityRef entity) = 0;
	virtual void setBoneAttachmentPosition(EntityRef entity, const Vec3& pos) = 0;
	virtual Vec3 getBoneAttachmentRotation(EntityRef entity) = 0;
	virtual void setBoneAttachmentRotation(EntityRef entity, const Vec3& rot) = 0;
	virtual void setBoneAttachmentRotationQuat(EntityRef entity, const Quat& rot) = 0;

	virtual const Array<DebugTriangle>& getDebugTriangles() const = 0;
	virtual const Array<DebugLine>& getDebugLines() const = 0;
	virtual const Array<DebugPoint>& getDebugPoints() const = 0;

	virtual Matrix getCameraProjection(EntityRef entity) = 0;
	virtual Matrix getCameraViewProjection(EntityRef entity) = 0;
	virtual float getCameraFOV(EntityRef entity) = 0;
	virtual void setCameraFOV(EntityRef entity, float fov) = 0;
	virtual void setCameraFarPlane(EntityRef entity, float far) = 0;
	virtual void setCameraNearPlane(EntityRef entity, float near) = 0;
	virtual float getCameraFarPlane(EntityRef entity) = 0;
	virtual float getCameraNearPlane(EntityRef entity) = 0;
	virtual float getCameraScreenWidth(EntityRef entity) = 0;
	virtual float getCameraScreenHeight(EntityRef entity) = 0;
	virtual void setCameraScreenSize(EntityRef entity, int w, int h) = 0;
	virtual bool isCameraOrtho(EntityRef entity) = 0;
	virtual void setCameraOrtho(EntityRef entity, bool is_ortho) = 0;
	virtual float getCameraOrthoSize(EntityRef entity) = 0;
	virtual void setCameraOrthoSize(EntityRef entity, float value) = 0;
	virtual Vec2 getCameraScreenSize(EntityRef entity) = 0;

	virtual void setParticleEmitterPath(EntityRef entity, const Path& path) = 0;
	virtual Path getParticleEmitterPath(EntityRef entity) = 0;
	virtual const AssociativeArray<EntityRef, class ParticleEmitter*>& getParticleEmitters() const = 0;

	virtual void enableModelInstance(EntityRef entity, bool enable) = 0;
	virtual bool isModelInstanceEnabled(EntityRef entity) = 0;
	virtual ModelInstance* getModelInstance(EntityRef entity) = 0;
	virtual const ModelInstance* getModelInstances() const = 0;
	virtual Path getModelInstancePath(EntityRef entity) = 0;
	virtual void setModelInstancePath(EntityRef entity, const Path& path) = 0;
	virtual void getRenderables(const ShiftedFrustum& frustum, Array<Array<u32>>& result) const = 0;
	virtual void getModelInstanceEntities(const ShiftedFrustum& frustum, Array<EntityRef>& entities) = 0;
	virtual EntityPtr getFirstModelInstance() = 0;
	virtual EntityPtr getNextModelInstance(EntityPtr entity) = 0;
	virtual Model* getModelInstanceModel(EntityRef entity) = 0;

	virtual void setDecalMaterialPath(EntityRef entity, const Path& path) = 0;
	virtual Path getDecalMaterialPath(EntityRef entity) = 0;
	virtual void setDecalHalfExtents(EntityRef entity, const Vec3& value) = 0;
	virtual Vec3 getDecalHalfExtents(EntityRef entity) = 0;
	virtual Material* getDecalMaterial(EntityRef entity) const = 0;

	virtual void getGrassInfos(const Frustum& frustum,
		EntityRef entity,
		Array<GrassInfo>& infos) = 0;
	virtual void forceGrassUpdate(EntityRef entity) = 0;
	virtual Terrain* getTerrain(EntityRef entity) = 0;
	virtual void getTerrainInfos(const ShiftedFrustum& frustum, const DVec3& lod_ref_point, Array<TerrainInfo>& infos) = 0;
	virtual float getTerrainHeightAt(EntityRef entity, float x, float z) = 0;
	virtual Vec3 getTerrainNormalAt(EntityRef entity, float x, float z) = 0;
	virtual void setTerrainHeightmapPath(EntityRef entity, const Path& path) = 0;
	virtual Path getTerrainHeightmapPath(EntityRef entity) = 0;
	virtual void setTerrainSplatmapPath(EntityRef entity, const Path& path) = 0;
	virtual Path getTerrainSplatmapPath(EntityRef entity) = 0;
	virtual void setTerrainDetailTexturesPath(EntityRef entity, const Path& path) = 0;
	virtual Path getTerrainDetailTexturesPath(EntityRef entity) = 0;
	virtual void setTerrainMaterialPath(EntityRef entity, const Path& path) = 0;
	virtual Path getTerrainMaterialPath(EntityRef entity) = 0;
	virtual Material* getTerrainMaterial(EntityRef entity) = 0;
	virtual void setTerrainXZScale(EntityRef entity, float scale) = 0;
	virtual float getTerrainXZScale(EntityRef entity) = 0;
	virtual void setTerrainYScale(EntityRef entity, float scale) = 0;
	virtual float getTerrainYScale(EntityRef entity) = 0;
	virtual Vec2 getTerrainSize(EntityRef entity) = 0;
	virtual AABB getTerrainAABB(EntityRef entity) = 0;
	virtual Vec2 getTerrainResolution(EntityRef entity) = 0;
	virtual EntityPtr getFirstTerrain() = 0;
	virtual EntityPtr getNextTerrain(EntityRef entity) = 0;

	virtual bool isGrassEnabled() const = 0;
	virtual int getGrassRotationMode(EntityRef entity, int index) = 0;
	virtual void setGrassRotationMode(EntityRef entity, int index, int value) = 0;
	virtual float getGrassDistance(EntityRef entity, int index) = 0;
	virtual void setGrassDistance(EntityRef entity, int index, float value) = 0;
	virtual void enableGrass(bool enabled) = 0;
	virtual void setGrassPath(EntityRef entity, int index, const Path& path) = 0;
	virtual Path getGrassPath(EntityRef entity, int index) = 0;
	virtual void setGrassDensity(EntityRef entity, int index, int density) = 0;
	virtual int getGrassDensity(EntityRef entity, int index) = 0;
	virtual int getGrassCount(EntityRef entity) = 0;
	virtual void addGrass(EntityRef entity, int index) = 0;
	virtual void removeGrass(EntityRef entity, int index) = 0;

	virtual void setLightCastShadows(EntityRef entity, bool cast_shadows) = 0;
	virtual bool getLightCastShadows(EntityRef entity) = 0;
	virtual float getLightAttenuation(EntityRef entity) = 0;
	virtual void setLightAttenuation(EntityRef entity, float attenuation) = 0;
	virtual float getLightFOV(EntityRef entity) = 0;
	virtual void setLightFOV(EntityRef entity, float fov) = 0;
	virtual float getLightRange(EntityRef entity) = 0;
	virtual void setLightRange(EntityRef entity, float value) = 0;
	virtual void setPointLightIntensity(EntityRef entity, float intensity) = 0;
	virtual void setGlobalLightIntensity(EntityRef entity, float intensity) = 0;
	virtual void setGlobalLightIndirectIntensity(EntityRef entity, float intensity) = 0;
	virtual void setPointLightColor(EntityRef entity, const Vec3& color) = 0;
	virtual void setGlobalLightColor(EntityRef entity, const Vec3& color) = 0;
	virtual void setFogDensity(EntityRef entity, float density) = 0;
	virtual void setFogColor(EntityRef entity, const Vec3& color) = 0;
	virtual float getPointLightIntensity(EntityRef entity) = 0;
	virtual EntityRef getPointLightEntity(EntityRef entity) const = 0;
	virtual EntityRef getGlobalLightEntity(EntityRef entity) const = 0;
	virtual float getGlobalLightIntensity(EntityRef entity) = 0;
	virtual float getGlobalLightIndirectIntensity(EntityRef entity) = 0;
	virtual Vec3 getPointLightColor(EntityRef entity) = 0;
	virtual Vec3 getGlobalLightColor(EntityRef entity) = 0;
	virtual float getFogDensity(EntityRef entity) = 0;
	virtual float getFogBottom(EntityRef entity) = 0;
	virtual float getFogHeight(EntityRef entity) = 0;
	virtual void setFogBottom(EntityRef entity, float value) = 0;
	virtual void setFogHeight(EntityRef entity, float value) = 0;
	virtual Vec3 getFogColor(EntityRef entity) = 0;
	virtual Vec3 getPointLightSpecularColor(EntityRef entity) = 0;
	virtual void setPointLightSpecularColor(EntityRef entity, const Vec3& color) = 0;
	virtual float getPointLightSpecularIntensity(EntityRef entity) = 0;
	virtual void setPointLightSpecularIntensity(EntityRef entity, float color) = 0;

	virtual void enableEnvironmentProbe(EntityRef entity, bool enable) = 0;
	virtual bool isEnvironmentProbeEnabled(EntityRef entity) = 0;
	virtual void getEnvironmentProbes(Array<EnvProbeInfo>& probes) = 0;
	virtual int getEnvironmentProbeIrradianceSize(EntityRef entity) = 0;
	virtual void setEnvironmentProbeIrradianceSize(EntityRef entity, int size) = 0;
	virtual int getEnvironmentProbeRadianceSize(EntityRef entity) = 0;
	virtual void setEnvironmentProbeRadianceSize(EntityRef entity, int size) = 0;
	virtual int getEnvironmentProbeReflectionSize(EntityRef entity) = 0;
	virtual void setEnvironmentProbeReflectionSize(EntityRef entity, int size) = 0;
	virtual bool isEnvironmentProbeReflectionEnabled(EntityRef entity) = 0;
	virtual void enableEnvironmentProbeReflection(EntityRef entity, bool enable) = 0;
	virtual bool isEnvironmentProbeCustomSize(EntityRef entity) = 0;
	virtual void enableEnvironmentProbeCustomSize(EntityRef entity, bool enable) = 0;
	virtual Texture* getEnvironmentProbeTexture(EntityRef entity) const = 0;
	virtual Texture* getEnvironmentProbeIrradiance(EntityRef entity) const = 0;
	virtual Texture* getEnvironmentProbeRadiance(EntityRef entity) const = 0;
	virtual void reloadEnvironmentProbe(EntityRef entity) = 0;
	virtual u64 getEnvironmentProbeGUID(EntityRef entity) const = 0;
	virtual float getEnvironmentProbeRadius(EntityRef entity) = 0;
	virtual void setEnvironmentProbeRadius(EntityRef entity, float radius) = 0;

	virtual void setTextMeshText(EntityRef entity, const char* text) = 0;
	virtual const char* getTextMeshText(EntityRef entity) = 0;
	virtual void setTextMeshFontSize(EntityRef entity, int value) = 0;
	virtual int getTextMeshFontSize(EntityRef entity) = 0;
	virtual Vec4 getTextMeshColorRGBA(EntityRef entity) = 0;
	virtual void setTextMeshColorRGBA(EntityRef entity, const Vec4& color) = 0;
	virtual Path getTextMeshFontPath(EntityRef entity) = 0;
	virtual void setTextMeshFontPath(EntityRef entity, const Path& path) = 0;
	virtual bool isTextMeshCameraOriented(EntityRef entity) = 0;
	virtual void setTextMeshCameraOriented(EntityRef entity, bool is_oriented) = 0;
	virtual void getTextMeshesVertices(Array<TextMeshVertex>& vertices, const DVec3& cam_pos, const Quat& rot) = 0;

protected:
	virtual ~RenderScene() {}
};
}