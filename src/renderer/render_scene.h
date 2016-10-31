#pragma once


#include "engine/lumix.h"
#include "engine/matrix.h"
#include "engine/iplugin.h"


struct lua_State;


namespace Lumix
{

struct AABB;
class Engine;
struct Frustum;
class IAllocator;
class LIFOAllocator;
class Material;
struct Mesh;
class Model;
class Path;
struct  Pose;
struct RayCastModelHit;
class Renderer;
class Shader;
class Terrain;
class Texture;
class Universe;
template <typename T> class Array;
template <typename T, typename T2> class AssociativeArray;
template <typename T> class DelegateList;


enum class RenderSceneVersion : int32
{
	PARTICLES,
	WHOLE_LIGHTS,
	PARTICLE_EMITTERS_SPAWN_COUNT,
	PARTICLES_FORCE_MODULE,
	PARTICLES_SAVE_SIZE_ALPHA,
	MODEL_INSTANCE_MATERIALS,
	GLOBAL_LIGHT_SPECULAR,
	SPECULAR_INTENSITY,
	RENDER_PARAMS,
	RENDER_PARAMS_REMOVED,
	GRASS_TYPE_DISTANCE,
	ORTHO_CAMERA,
	BONE_ATTACHMENTS,
	ENVIRONMENT_PROBES,
	FOV_RADIANS,
	INDEPENDENT_PARTICLE_MODULES,
	CAMERA_AND_TERRAIN_REFACTOR,
	DECAL,
	PARTICLE_EMITTER_SUBIMAGE_MODULE,
	PARTICLE_EMITTER_LOCAL_SPACE,
	NEW_GRASS,
	LAYERS,

	LATEST,
	INVALID = -1,
};


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
	enum Type
	{
		RIGID,
		SKINNED,
		MULTILAYER
	};
	Type type;
	Matrix matrix;
	Model* model;
	Pose* pose;
	Entity entity;
	Mesh* meshes;
	bool custom_meshes;
	int8 mesh_count;
};


struct ModelInstanceMesh
{
	ComponentHandle model_instance;
	Mesh* mesh;
};


struct GrassInfo
{
	struct InstanceData
	{
		Matrix matrix;
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
	uint32 color;
	float life;
};


struct DebugLine
{
	Vec3 from;
	Vec3 to;
	uint32 color;
	float life;
};


struct DebugPoint
{
	Vec3 pos;
	uint32 color;
	float life;
};


class LUMIX_RENDERER_API RenderScene : public IScene
{
public:
	static RenderScene* createInstance(Renderer& renderer,
		Engine& engine,
		Universe& universe,
		bool is_forward_rendered,
		IAllocator& allocator);
	static void destroyInstance(RenderScene* scene);
	static void registerLuaAPI(lua_State* L);

	virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, ComponentHandle ignore) = 0;
	virtual RayCastModelHit castRayTerrain(ComponentHandle terrain, const Vec3& origin, const Vec3& dir) = 0;
	virtual void getRay(ComponentHandle camera, float x, float y, Vec3& origin, Vec3& dir) = 0;

	virtual Frustum getCameraFrustum(ComponentHandle camera) const = 0;
	virtual float getTime() const = 0;
	virtual Engine& getEngine() const = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual Pose* getPose(ComponentHandle cmp) = 0;
	virtual ComponentHandle getActiveGlobalLight() = 0;
	virtual void setActiveGlobalLight(ComponentHandle cmp) = 0;
	virtual Vec4 getShadowmapCascades(ComponentHandle cmp) = 0;
	virtual void setShadowmapCascades(ComponentHandle cmp, const Vec4& value) = 0;

	virtual void addDebugTriangle(const Vec3& p0,
		const Vec3& p1,
		const Vec3& p2,
		uint32 color,
		float life) = 0;
	virtual void addDebugPoint(const Vec3& pos, uint32 color, float life) = 0;
	virtual void addDebugCone(const Vec3& vertex,
		const Vec3& dir,
		const Vec3& axis0,
		const Vec3& axis1,
		uint32 color,
		float life) = 0;

	virtual void addDebugLine(const Vec3& from, const Vec3& to, uint32 color, float life) = 0;
	virtual void addDebugCross(const Vec3& center, float size, uint32 color, float life) = 0;
	virtual void addDebugCube(const Vec3& pos,
		const Vec3& dir,
		const Vec3& up,
		const Vec3& right,
		uint32 color,
		float life) = 0;
	virtual void addDebugCube(const Vec3& from, const Vec3& max, uint32 color, float life) = 0;
	virtual void addDebugCubeSolid(const Vec3& from, const Vec3& max, uint32 color, float life) = 0;
	virtual void addDebugCircle(const Vec3& center,
		const Vec3& up,
		float radius,
		uint32 color,
		float life) = 0;
	virtual void addDebugSphere(const Vec3& center, float radius, uint32 color, float life) = 0;
	virtual void addDebugFrustum(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float fov,
		float ratio,
		float near_distance,
		float far_distance,
		uint32 color,
		float life) = 0;

	virtual void addDebugFrustum(const Frustum& frustum, uint32 color, float life) = 0;

	virtual void addDebugCapsule(const Vec3& position,
		float height,
		float radius,
		uint32 color,
		float life) = 0;

	virtual void addDebugCapsule(const Matrix& transform,
		float height,
		float radius,
		uint32 color,
		float life) = 0;

	virtual void addDebugCylinder(const Vec3& position,
		const Vec3& up,
		float radius,
		uint32 color,
		float life) = 0;

	virtual Entity getBoneAttachmentParent(ComponentHandle cmp) = 0;
	virtual void setBoneAttachmentParent(ComponentHandle cmp, Entity entity) = 0;
	virtual void setBoneAttachmentBone(ComponentHandle cmp, int value) = 0;
	virtual int getBoneAttachmentBone(ComponentHandle cmp) = 0;
	virtual Vec3 getBoneAttachmentPosition(ComponentHandle cmp) = 0;
	virtual void setBoneAttachmentPosition(ComponentHandle cmp, const Vec3& pos) = 0;
	virtual Vec3 getBoneAttachmentRotation(ComponentHandle cmp) = 0;
	virtual void setBoneAttachmentRotation(ComponentHandle cmp, const Vec3& rot) = 0;

	virtual const Array<DebugTriangle>& getDebugTriangles() const = 0;
	virtual const Array<DebugLine>& getDebugLines() const = 0;
	virtual const Array<DebugPoint>& getDebugPoints() const = 0;

	virtual Matrix getCameraProjection(ComponentHandle camera) = 0;
	virtual Matrix getCameraViewProjection(ComponentHandle camera) = 0;
	virtual Entity getCameraEntity(ComponentHandle camera) const = 0;
	virtual ComponentHandle getCameraInSlot(const char* slot) = 0;
	virtual float getCameraFOV(ComponentHandle camera) = 0;
	virtual void setCameraFOV(ComponentHandle camera, float fov) = 0;
	virtual void setCameraFarPlane(ComponentHandle camera, float far) = 0;
	virtual void setCameraNearPlane(ComponentHandle camera, float near) = 0;
	virtual float getCameraFarPlane(ComponentHandle camera) = 0;
	virtual float getCameraNearPlane(ComponentHandle camera) = 0;
	virtual float getCameraScreenWidth(ComponentHandle camera) = 0;
	virtual float getCameraScreenHeight(ComponentHandle camera) = 0;
	virtual void setCameraSlot(ComponentHandle camera, const char* slot) = 0;
	virtual const char* getCameraSlot(ComponentHandle camera) = 0;
	virtual void setCameraScreenSize(ComponentHandle camera, int w, int h) = 0;
	virtual bool isCameraOrtho(ComponentHandle camera) = 0;
	virtual void setCameraOrtho(ComponentHandle camera, bool is_ortho) = 0;
	virtual float getCameraOrthoSize(ComponentHandle camera) = 0;
	virtual void setCameraOrthoSize(ComponentHandle camera, float value) = 0;
	virtual Vec2 getCameraScreenSize(ComponentHandle camera) = 0;

	virtual class ParticleEmitter* getParticleEmitter(ComponentHandle cmp) = 0;
	virtual void resetParticleEmitter(ComponentHandle cmp) = 0;
	virtual void updateEmitter(ComponentHandle cmp, float time_delta) = 0;
	virtual const AssociativeArray<Entity, class ParticleEmitter*>& getParticleEmitters() const = 0;
	virtual const Vec2* getParticleEmitterAlpha(ComponentHandle cmp) = 0;
	virtual int getParticleEmitterAlphaCount(ComponentHandle cmp) = 0;
	virtual const Vec2* getParticleEmitterSize(ComponentHandle cmp) = 0;
	virtual int getParticleEmitterSizeCount(ComponentHandle cmp) = 0;
	virtual bool getParticleEmitterAutoemit(ComponentHandle cmp) = 0;
	virtual bool getParticleEmitterLocalSpace(ComponentHandle cmp) = 0;
	virtual Vec3 getParticleEmitterAcceleration(ComponentHandle cmp) = 0;
	virtual Vec2 getParticleEmitterLinearMovementX(ComponentHandle cmp) = 0;
	virtual Vec2 getParticleEmitterLinearMovementY(ComponentHandle cmp) = 0;
	virtual Vec2 getParticleEmitterLinearMovementZ(ComponentHandle cmp) = 0;
	virtual Vec2 getParticleEmitterInitialLife(ComponentHandle cmp) = 0;
	virtual Int2 getParticleEmitterSpawnCount(ComponentHandle cmp) = 0;
	virtual Vec2 getParticleEmitterSpawnPeriod(ComponentHandle cmp) = 0;
	virtual Vec2 getParticleEmitterInitialSize(ComponentHandle cmp) = 0;
	virtual void setParticleEmitterAutoemit(ComponentHandle cmp, bool autoemit) = 0;
	virtual void setParticleEmitterLocalSpace(ComponentHandle cmp, bool autoemit) = 0;
	virtual void setParticleEmitterAlpha(ComponentHandle cmp, const Vec2* value, int count) = 0;
	virtual void setParticleEmitterSize(ComponentHandle cmp, const Vec2* values, int count) = 0;
	virtual void setParticleEmitterAcceleration(ComponentHandle cmp, const Vec3& value) = 0;
	virtual void setParticleEmitterLinearMovementX(ComponentHandle cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterLinearMovementY(ComponentHandle cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterLinearMovementZ(ComponentHandle cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterInitialLife(ComponentHandle cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterSpawnCount(ComponentHandle cmp, const Int2& value) = 0;
	virtual void setParticleEmitterSpawnPeriod(ComponentHandle cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterInitialSize(ComponentHandle cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterMaterialPath(ComponentHandle cmp, const Path& path) = 0;
	virtual void setParticleEmitterSubimageRows(ComponentHandle cmp, const int& value) = 0;
	virtual void setParticleEmitterSubimageCols(ComponentHandle cmp, const int& value) = 0;
	virtual Path getParticleEmitterMaterialPath(ComponentHandle cmp) = 0;
	virtual int getParticleEmitterPlaneCount(ComponentHandle cmp) = 0;
	virtual int getParticleEmitterSubimageRows(ComponentHandle cmp) = 0;
	virtual int getParticleEmitterSubimageCols(ComponentHandle cmp) = 0;
	virtual void addParticleEmitterPlane(ComponentHandle cmp, int index) = 0;
	virtual void removeParticleEmitterPlane(ComponentHandle cmp, int index) = 0;
	virtual Entity getParticleEmitterPlaneEntity(ComponentHandle cmp, int index) = 0;
	virtual void setParticleEmitterPlaneEntity(ComponentHandle cmp, int index, Entity entity) = 0;
	virtual float getParticleEmitterPlaneBounce(ComponentHandle cmp) = 0;
	virtual void setParticleEmitterPlaneBounce(ComponentHandle cmp, float value) = 0;
	virtual float getParticleEmitterShapeRadius(ComponentHandle cmp) = 0;
	virtual void setParticleEmitterShapeRadius(ComponentHandle cmp, float value) = 0;

	virtual int getParticleEmitterAttractorCount(ComponentHandle cmp) = 0;
	virtual void addParticleEmitterAttractor(ComponentHandle cmp, int index) = 0;
	virtual void removeParticleEmitterAttractor(ComponentHandle cmp, int index) = 0;
	virtual Entity getParticleEmitterAttractorEntity(ComponentHandle cmp, int index) = 0;
	virtual void setParticleEmitterAttractorEntity(ComponentHandle cmp,
		int index,
		Entity entity) = 0;
	virtual float getParticleEmitterAttractorForce(ComponentHandle cmp) = 0;
	virtual void setParticleEmitterAttractorForce(ComponentHandle cmp, float value) = 0;

	virtual DelegateList<void(ComponentHandle)>& model_instanceCreated() = 0;
	virtual DelegateList<void(ComponentHandle)>& model_instanceDestroyed() = 0;
	virtual void showModelInstance(ComponentHandle cmp) = 0;
	virtual void hideModelInstance(ComponentHandle cmp) = 0;
	virtual ComponentHandle getModelInstanceComponent(Entity entity) = 0;
	virtual ModelInstance* getModelInstance(ComponentHandle cmp) = 0;
	virtual ModelInstance* getModelInstances() = 0;
	virtual Path getModelInstancePath(ComponentHandle cmp) = 0;
	virtual void setModelInstanceMaterial(ComponentHandle cmp, int index, const Path& path) = 0;
	virtual Path getModelInstanceMaterial(ComponentHandle cmp, int index) = 0;
	virtual int getModelInstanceMaterialsCount(ComponentHandle cmp) = 0;
	virtual void setModelInstancePath(ComponentHandle cmp, const Path& path) = 0;
	virtual Array<Array<ModelInstanceMesh>>& getModelInstanceInfos(const Frustum& frustum,
		const Vec3& lod_ref_point,
		uint64 layer_mask) = 0;
	virtual void getModelInstanceEntities(const Frustum& frustum, Array<Entity>& entities) = 0;
	virtual Entity getModelInstanceEntity(ComponentHandle cmp) = 0;
	virtual ComponentHandle getFirstModelInstance() = 0;
	virtual ComponentHandle getNextModelInstance(ComponentHandle cmp) = 0;
	virtual Model* getModelInstanceModel(ComponentHandle cmp) = 0;

	virtual void setDecalMaterialPath(ComponentHandle cmp, const Path& path) = 0;
	virtual Path getDecalMaterialPath(ComponentHandle cmp) = 0;
	virtual void setDecalScale(ComponentHandle cmp, const Vec3& value) = 0;
	virtual Vec3 getDecalScale(ComponentHandle cmp) = 0;
	virtual void getDecals(const Frustum& frustum, Array<DecalInfo>& decals) = 0;

	virtual void getGrassInfos(const Frustum& frustum,
		Array<GrassInfo>& infos,
		ComponentHandle camera) = 0;
	virtual void forceGrassUpdate(ComponentHandle cmp) = 0;
	virtual void getTerrainInfos(Array<TerrainInfo>& infos, const Vec3& camera_pos) = 0;
	virtual float getTerrainHeightAt(ComponentHandle cmp, float x, float z) = 0;
	virtual Vec3 getTerrainNormalAt(ComponentHandle cmp, float x, float z) = 0;
	virtual void setTerrainMaterialPath(ComponentHandle cmp, const Path& path) = 0;
	virtual Path getTerrainMaterialPath(ComponentHandle cmp) = 0;
	virtual Material* getTerrainMaterial(ComponentHandle cmp) = 0;
	virtual void setTerrainXZScale(ComponentHandle cmp, float scale) = 0;
	virtual float getTerrainXZScale(ComponentHandle cmp) = 0;
	virtual void setTerrainYScale(ComponentHandle cmp, float scale) = 0;
	virtual float getTerrainYScale(ComponentHandle cmp) = 0;
	virtual Vec2 getTerrainSize(ComponentHandle cmp) = 0;
	virtual AABB getTerrainAABB(ComponentHandle cmp) = 0;
	virtual ComponentHandle getTerrainComponent(Entity entity) = 0;
	virtual Entity getTerrainEntity(ComponentHandle cmp) = 0;
	virtual Vec2 getTerrainResolution(ComponentHandle cmp) = 0;
	virtual ComponentHandle getFirstTerrain() = 0;
	virtual ComponentHandle getNextTerrain(ComponentHandle cmp) = 0;

	virtual bool isGrassEnabled() const = 0;
	virtual float getGrassDistance(ComponentHandle cmp, int index) = 0;
	virtual void setGrassDistance(ComponentHandle cmp, int index, float value) = 0;
	virtual void enableGrass(bool enabled) = 0;
	virtual void setGrassPath(ComponentHandle cmp, int index, const Path& path) = 0;
	virtual Path getGrassPath(ComponentHandle cmp, int index) = 0;
	virtual void setGrassDensity(ComponentHandle cmp, int index, int density) = 0;
	virtual int getGrassDensity(ComponentHandle cmp, int index) = 0;
	virtual int getGrassCount(ComponentHandle cmp) = 0;
	virtual void addGrass(ComponentHandle cmp, int index) = 0;
	virtual void removeGrass(ComponentHandle cmp, int index) = 0;

	virtual int getClosestPointLights(const Vec3& pos, ComponentHandle* lights, int max_lights) = 0;
	virtual void getPointLights(const Frustum& frustum, Array<ComponentHandle>& lights) = 0;
	virtual void getPointLightInfluencedGeometry(ComponentHandle light_cmp,
		Array<ModelInstanceMesh>& infos) = 0;
	virtual void getPointLightInfluencedGeometry(ComponentHandle light_cmp,
		const Frustum& frustum,
		Array<ModelInstanceMesh>& infos) = 0;
	virtual void setLightCastShadows(ComponentHandle cmp, bool cast_shadows) = 0;
	virtual bool getLightCastShadows(ComponentHandle cmp) = 0;
	virtual float getLightAttenuation(ComponentHandle cmp) = 0;
	virtual void setLightAttenuation(ComponentHandle cmp, float attenuation) = 0;
	virtual float getLightFOV(ComponentHandle cmp) = 0;
	virtual void setLightFOV(ComponentHandle cmp, float fov) = 0;
	virtual float getLightRange(ComponentHandle cmp) = 0;
	virtual void setLightRange(ComponentHandle cmp, float value) = 0;
	virtual void setPointLightIntensity(ComponentHandle cmp, float intensity) = 0;
	virtual void setGlobalLightIntensity(ComponentHandle cmp, float intensity) = 0;
	virtual void setPointLightColor(ComponentHandle cmp, const Vec3& color) = 0;
	virtual void setGlobalLightColor(ComponentHandle cmp, const Vec3& color) = 0;
	virtual void setGlobalLightSpecular(ComponentHandle cmp, const Vec3& color) = 0;
	virtual void setGlobalLightSpecularIntensity(ComponentHandle cmp, float intensity) = 0;
	virtual void setLightAmbientIntensity(ComponentHandle cmp, float intensity) = 0;
	virtual void setLightAmbientColor(ComponentHandle cmp, const Vec3& color) = 0;
	virtual void setFogDensity(ComponentHandle cmp, float density) = 0;
	virtual void setFogColor(ComponentHandle cmp, const Vec3& color) = 0;
	virtual float getPointLightIntensity(ComponentHandle cmp) = 0;
	virtual Entity getPointLightEntity(ComponentHandle cmp) const = 0;
	virtual Entity getGlobalLightEntity(ComponentHandle cmp) const = 0;
	virtual float getGlobalLightIntensity(ComponentHandle cmp) = 0;
	virtual Vec3 getPointLightColor(ComponentHandle cmp) = 0;
	virtual Vec3 getGlobalLightColor(ComponentHandle cmp) = 0;
	virtual Vec3 getGlobalLightSpecular(ComponentHandle cmp) = 0;
	virtual float getGlobalLightSpecularIntensity(ComponentHandle cmp) = 0;
	virtual float getLightAmbientIntensity(ComponentHandle cmp) = 0;
	virtual Vec3 getLightAmbientColor(ComponentHandle cmp) = 0;
	virtual float getFogDensity(ComponentHandle cmp) = 0;
	virtual float getFogBottom(ComponentHandle cmp) = 0;
	virtual float getFogHeight(ComponentHandle cmp) = 0;
	virtual void setFogBottom(ComponentHandle cmp, float value) = 0;
	virtual void setFogHeight(ComponentHandle cmp, float value) = 0;
	virtual Vec3 getFogColor(ComponentHandle cmp) = 0;
	virtual Vec3 getPointLightSpecularColor(ComponentHandle cmp) = 0;
	virtual void setPointLightSpecularColor(ComponentHandle cmp, const Vec3& color) = 0;
	virtual float getPointLightSpecularIntensity(ComponentHandle cmp) = 0;
	virtual void setPointLightSpecularIntensity(ComponentHandle cmp, float color) = 0;

	virtual Texture* getEnvironmentProbeTexture(ComponentHandle cmp) const = 0;
	virtual void reloadEnvironmentProbe(ComponentHandle cmp) = 0;

protected:
	virtual ~RenderScene() {}
};
}