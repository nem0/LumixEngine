#pragma once


#include "lumix.h"
#include "core/matrix.h"
#include "iplugin.h"


struct lua_State;


namespace Lumix
{

struct AABB;
class Engine;
class Frustum;
class IAllocator;
class LIFOAllocator;
class Material;
struct Mesh;
class Model;
class Path;
class Pose;
struct RayCastModelHit;
class Renderer;
class Shader;
class Terrain;
class Universe;
template <typename T> class Array;
template <typename T> class DelegateList;


enum class RenderSceneVersion : int32
{
	PARTICLES,
	WHOLE_LIGHTS,
	PARTICLE_EMITTERS_SPAWN_COUNT,
	PARTICLES_FORCE_MODULE,
	PARTICLES_SAVE_SIZE_ALPHA,
	RENDERABLE_MATERIALS,
	GLOBAL_LIGHT_SPECULAR,
	SPECULAR_INTENSITY,
	RENDER_PARAMS,
	RENDER_PARAMS_REMOVED,
	GRASS_TYPE_DISTANCE,

	LATEST,
	INVALID = -1,
};


struct TerrainInfo
{
	Shader* m_shader;
	Terrain* m_terrain;
	Matrix m_world_matrix;
	Vec3 m_morph_const;
	float m_size;
	Vec3 m_min;
	int m_index;
};


struct Renderable
{
	Pose* pose;
	Model* model;
	Matrix matrix;
	Entity entity;
	int64 layer_mask;
	Mesh* meshes;
	bool custom_meshes;
	int8 mesh_count;
};


struct RenderableMesh
{
	ComponentIndex renderable;
	Mesh* mesh;
};


struct GrassInfo
{
	Model* model;
	const Matrix* matrices;
	int matrix_count;
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


enum class RenderableType
{
	SKINNED_MESH,
	RIGID_MESH,
	GRASS,
	TERRAIN
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

	virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, ComponentIndex ignore) = 0;

	virtual RayCastModelHit castRayTerrain(ComponentIndex terrain,
		const Vec3& origin,
		const Vec3& dir) = 0;

	virtual void getRay(ComponentIndex camera, float x, float y, Vec3& origin, Vec3& dir) = 0;

	virtual Frustum getCameraFrustum(ComponentIndex camera) const = 0;
	virtual float getTime() const = 0;
	virtual Engine& getEngine() const = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual Pose* getPose(ComponentIndex cmp) = 0;
	virtual ComponentIndex getActiveGlobalLight() = 0;
	virtual void setActiveGlobalLight(ComponentIndex cmp) = 0;
	virtual Vec4 getShadowmapCascades(ComponentIndex cmp) = 0;
	virtual void setShadowmapCascades(ComponentIndex cmp, const Vec4& value) = 0;

	virtual void addDebugTriangle(const Vec3& p0,
		const Vec3& p1,
		const Vec3& p2,
		uint32 color,
		float life) = 0;
	virtual void addDebugPoint(const Vec3& pos, uint32 color, float life) = 0;

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

	virtual void addDebugCylinder(const Vec3& position,
		const Vec3& up,
		float radius,
		uint32 color,
		float life) = 0;

	virtual const Array<DebugTriangle>& getDebugTriangles() const = 0;
	virtual const Array<DebugLine>& getDebugLines() const = 0;
	virtual const Array<DebugPoint>& getDebugPoints() const = 0;

	virtual Entity getCameraEntity(ComponentIndex camera) const = 0;
	virtual ComponentIndex getCameraInSlot(const char* slot) = 0;
	virtual float getCameraFOV(ComponentIndex camera) = 0;
	virtual void setCameraFOV(ComponentIndex camera, float fov) = 0;
	virtual void setCameraFarPlane(ComponentIndex camera, float far) = 0;
	virtual void setCameraNearPlane(ComponentIndex camera, float near) = 0;
	virtual float getCameraFarPlane(ComponentIndex camera) = 0;
	virtual float getCameraNearPlane(ComponentIndex camera) = 0;
	virtual float getCameraWidth(ComponentIndex camera) = 0;
	virtual float getCameraHeight(ComponentIndex camera) = 0;
	virtual void setCameraSlot(ComponentIndex camera, const char* slot) = 0;
	virtual const char* getCameraSlot(ComponentIndex camera) = 0;
	virtual void setCameraSize(ComponentIndex camera, int w, int h) = 0;

	virtual class ParticleEmitter* getParticleEmitter(ComponentIndex cmp) = 0;
	virtual void resetParticleEmitter(ComponentIndex cmp) = 0;
	virtual void updateEmitter(ComponentIndex cmp, float time_delta) = 0;
	virtual const Array<class ParticleEmitter*>& getParticleEmitters() const = 0;
	virtual const Vec2* getParticleEmitterAlpha(ComponentIndex cmp) = 0;
	virtual int getParticleEmitterAlphaCount(ComponentIndex cmp) = 0;
	virtual const Vec2* getParticleEmitterSize(ComponentIndex cmp) = 0;
	virtual int getParticleEmitterSizeCount(ComponentIndex cmp) = 0;
	virtual Vec3 getParticleEmitterAcceleration(ComponentIndex cmp) = 0;
	virtual Vec2 getParticleEmitterLinearMovementX(ComponentIndex cmp) = 0;
	virtual Vec2 getParticleEmitterLinearMovementY(ComponentIndex cmp) = 0;
	virtual Vec2 getParticleEmitterLinearMovementZ(ComponentIndex cmp) = 0;
	virtual Vec2 getParticleEmitterInitialLife(ComponentIndex cmp) = 0;
	virtual Int2 getParticleEmitterSpawnCount(ComponentIndex cmp) = 0;
	virtual Vec2 getParticleEmitterSpawnPeriod(ComponentIndex cmp) = 0;
	virtual Vec2 getParticleEmitterInitialSize(ComponentIndex cmp) = 0;
	virtual void setParticleEmitterAlpha(ComponentIndex cmp, const Vec2* value, int count) = 0;
	virtual void setParticleEmitterSize(ComponentIndex cmp, const Vec2* values, int count) = 0;
	virtual void setParticleEmitterAcceleration(ComponentIndex cmp, const Vec3& value) = 0;
	virtual void setParticleEmitterLinearMovementX(ComponentIndex cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterLinearMovementY(ComponentIndex cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterLinearMovementZ(ComponentIndex cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterInitialLife(ComponentIndex cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterSpawnCount(ComponentIndex cmp, const Int2& value) = 0;
	virtual void setParticleEmitterSpawnPeriod(ComponentIndex cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterInitialSize(ComponentIndex cmp, const Vec2& value) = 0;
	virtual void setParticleEmitterMaterialPath(ComponentIndex cmp, const Path& path) = 0;
	virtual Path getParticleEmitterMaterialPath(ComponentIndex cmp) = 0;
	virtual int getParticleEmitterPlaneCount(ComponentIndex cmp) = 0;
	virtual void addParticleEmitterPlane(ComponentIndex cmp, int index) = 0;
	virtual void removeParticleEmitterPlane(ComponentIndex cmp, int index) = 0;
	virtual Entity getParticleEmitterPlaneEntity(ComponentIndex cmp, int index) = 0;
	virtual void setParticleEmitterPlaneEntity(ComponentIndex cmp, int index, Entity entity) = 0;
	virtual float getParticleEmitterPlaneBounce(ComponentIndex cmp) = 0;
	virtual void setParticleEmitterPlaneBounce(ComponentIndex cmp, float value) = 0;
	virtual float getParticleEmitterShapeRadius(ComponentIndex cmp) = 0;
	virtual void setParticleEmitterShapeRadius(ComponentIndex cmp, float value) = 0;

	virtual int getParticleEmitterAttractorCount(ComponentIndex cmp) = 0;
	virtual void addParticleEmitterAttractor(ComponentIndex cmp, int index) = 0;
	virtual void removeParticleEmitterAttractor(ComponentIndex cmp, int index) = 0;
	virtual Entity getParticleEmitterAttractorEntity(ComponentIndex cmp, int index) = 0;
	virtual void setParticleEmitterAttractorEntity(ComponentIndex cmp,
		int index,
		Entity entity) = 0;
	virtual float getParticleEmitterAttractorForce(ComponentIndex cmp) = 0;
	virtual void setParticleEmitterAttractorForce(ComponentIndex cmp, float value) = 0;

	virtual DelegateList<void(ComponentIndex)>& renderableCreated() = 0;
	virtual DelegateList<void(ComponentIndex)>& renderableDestroyed() = 0;
	virtual void showRenderable(ComponentIndex cmp) = 0;
	virtual void hideRenderable(ComponentIndex cmp) = 0;
	virtual ComponentIndex getRenderableComponent(Entity entity) = 0;
	virtual Renderable* getRenderable(ComponentIndex cmp) = 0;
	virtual Renderable* getRenderables() = 0;
	virtual Path getRenderablePath(ComponentIndex cmp) = 0;
	virtual void setRenderableMaterial(ComponentIndex cmp, int index, const Path& path) = 0;
	virtual Path getRenderableMaterial(ComponentIndex cmp, int index) = 0;
	virtual int getRenderableMaterialsCount(ComponentIndex cmp) = 0;
	virtual void setRenderableLayer(ComponentIndex cmp, const int32& layer) = 0;
	virtual void setRenderablePath(ComponentIndex cmp, const Path& path) = 0;
	virtual Array<Array<RenderableMesh>>& getRenderableInfos(const Frustum& frustum,
		const Vec3& lod_ref_point) = 0;
	virtual void getRenderableEntities(const Frustum& frustum, Array<Entity>& entities) = 0;
	virtual Entity getRenderableEntity(ComponentIndex cmp) = 0;
	virtual ComponentIndex getFirstRenderable() = 0;
	virtual ComponentIndex getNextRenderable(ComponentIndex cmp) = 0;
	virtual Model* getRenderableModel(ComponentIndex cmp) = 0;

	virtual void getGrassInfos(const Frustum& frustum,
		Array<GrassInfo>& infos,
		ComponentIndex camera) = 0;
	virtual void forceGrassUpdate(ComponentIndex cmp) = 0;
	virtual void getTerrainInfos(Array<const TerrainInfo*>& infos,
		const Vec3& camera_pos,
		LIFOAllocator& allocator) = 0;
	virtual float getTerrainHeightAt(ComponentIndex cmp, float x, float z) = 0;
	virtual Vec3 getTerrainNormalAt(ComponentIndex cmp, float x, float z) = 0;
	virtual void setTerrainMaterialPath(ComponentIndex cmp, const Path& path) = 0;
	virtual Path getTerrainMaterialPath(ComponentIndex cmp) = 0;
	virtual Material* getTerrainMaterial(ComponentIndex cmp) = 0;
	virtual void setTerrainXZScale(ComponentIndex cmp, float scale) = 0;
	virtual float getTerrainXZScale(ComponentIndex cmp) = 0;
	virtual void setTerrainYScale(ComponentIndex cmp, float scale) = 0;
	virtual float getTerrainYScale(ComponentIndex cmp) = 0;
	virtual Vec2 getTerrainSize(ComponentIndex cmp) = 0;
	virtual AABB getTerrainAABB(ComponentIndex cmp) = 0;
	virtual ComponentIndex getTerrainComponent(Entity entity) = 0;
	virtual Entity getTerrainEntity(ComponentIndex cmp) = 0;
	virtual Vec2 getTerrainResolution(ComponentIndex cmp) = 0;
	virtual ComponentIndex getFirstTerrain() = 0;
	virtual ComponentIndex getNextTerrain(ComponentIndex cmp) = 0;

	virtual bool isGrassEnabled() const = 0;
	virtual float getGrassDistance(ComponentIndex cmp, int index) = 0;
	virtual void setGrassDistance(ComponentIndex cmp, int index, float value) = 0;
	virtual void enableGrass(bool enabled) = 0;
	virtual void setGrassPath(ComponentIndex cmp, int index, const Path& path) = 0;
	virtual Path getGrassPath(ComponentIndex cmp, int index) = 0;
	virtual void setGrassGround(ComponentIndex cmp, int index, int ground) = 0;
	virtual int getGrassGround(ComponentIndex cmp, int index) = 0;
	virtual void setGrassDensity(ComponentIndex cmp, int index, int density) = 0;
	virtual int getGrassDensity(ComponentIndex cmp, int index) = 0;
	virtual int getGrassCount(ComponentIndex cmp) = 0;
	virtual void addGrass(ComponentIndex cmp, int index) = 0;
	virtual void removeGrass(ComponentIndex cmp, int index) = 0;

	virtual int getClosestPointLights(const Vec3& pos, ComponentIndex* lights, int max_lights) = 0;
	virtual void getPointLights(const Frustum& frustum, Array<ComponentIndex>& lights) = 0;
	virtual void getPointLightInfluencedGeometry(ComponentIndex light_cmp,
		Array<RenderableMesh>& infos) = 0;
	virtual void getPointLightInfluencedGeometry(ComponentIndex light_cmp,
		const Frustum& frustum,
		Array<RenderableMesh>& infos) = 0;
	virtual void setLightCastShadows(ComponentIndex cmp, bool cast_shadows) = 0;
	virtual bool getLightCastShadows(ComponentIndex cmp) = 0;
	virtual float getLightAttenuation(ComponentIndex cmp) = 0;
	virtual void setLightAttenuation(ComponentIndex cmp, float attenuation) = 0;
	virtual float getLightFOV(ComponentIndex cmp) = 0;
	virtual void setLightFOV(ComponentIndex cmp, float fov) = 0;
	virtual float getLightRange(ComponentIndex cmp) = 0;
	virtual void setLightRange(ComponentIndex cmp, float value) = 0;
	virtual void setPointLightIntensity(ComponentIndex cmp, float intensity) = 0;
	virtual void setGlobalLightIntensity(ComponentIndex cmp, float intensity) = 0;
	virtual void setPointLightColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual void setGlobalLightColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual void setGlobalLightSpecular(ComponentIndex cmp, const Vec3& color) = 0;
	virtual void setGlobalLightSpecularIntensity(ComponentIndex cmp, float intensity) = 0;
	virtual void setLightAmbientIntensity(ComponentIndex cmp, float intensity) = 0;
	virtual void setLightAmbientColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual void setFogDensity(ComponentIndex cmp, float density) = 0;
	virtual void setFogColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual float getPointLightIntensity(ComponentIndex cmp) = 0;
	virtual Entity getPointLightEntity(ComponentIndex cmp) const = 0;
	virtual Entity getGlobalLightEntity(ComponentIndex cmp) const = 0;
	virtual float getGlobalLightIntensity(ComponentIndex cmp) = 0;
	virtual Vec3 getPointLightColor(ComponentIndex cmp) = 0;
	virtual Vec3 getGlobalLightColor(ComponentIndex cmp) = 0;
	virtual Vec3 getGlobalLightSpecular(ComponentIndex cmp) = 0;
	virtual float getGlobalLightSpecularIntensity(ComponentIndex cmp) = 0;
	virtual float getLightAmbientIntensity(ComponentIndex cmp) = 0;
	virtual Vec3 getLightAmbientColor(ComponentIndex cmp) = 0;
	virtual float getFogDensity(ComponentIndex cmp) = 0;
	virtual float getFogBottom(ComponentIndex cmp) = 0;
	virtual float getFogHeight(ComponentIndex cmp) = 0;
	virtual void setFogBottom(ComponentIndex cmp, float value) = 0;
	virtual void setFogHeight(ComponentIndex cmp, float value) = 0;
	virtual Vec3 getFogColor(ComponentIndex cmp) = 0;
	virtual Vec3 getPointLightSpecularColor(ComponentIndex cmp) = 0;
	virtual void setPointLightSpecularColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual float getPointLightSpecularIntensity(ComponentIndex cmp) = 0;
	virtual void setPointLightSpecularIntensity(ComponentIndex cmp, float color) = 0;

protected:
	virtual ~RenderScene() {}
};
}