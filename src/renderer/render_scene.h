#pragma once

#include "lumix.h"
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/matrix.h"
#include "iplugin.h"
#include "renderer/ray_cast_model_hit.h"
#include "universe/component.h"


namespace Lumix
{

class Engine;
class Frustum;
class LIFOAllocator;
class Material;
class Mesh;
class Model;
class Path;
class PipelineInstance;
class Pose;
class Renderer;
class Shader;
class Terrain;
class Timer;
class Universe;


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
};


struct RenderableMesh
{
	ComponentIndex renderable;
	Mesh* mesh;
};


struct GrassInfo
{
	Model* m_model;
	const Matrix* m_matrices;
	int m_matrix_count;
};


struct DebugLine
{
	Vec3 m_from;
	Vec3 m_to;
	uint32 m_color;
	float m_life;
};


struct DebugPoint
{
	Vec3 m_pos;
	uint32 m_color;
	float m_life;
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

	virtual RayCastModelHit castRay(const Vec3& origin,
									const Vec3& dir,
									ComponentIndex ignore) = 0;

	virtual RayCastModelHit castRayTerrain(ComponentIndex terrain,
										   const Vec3& origin,
										   const Vec3& dir) = 0;

	virtual void getRay(
		ComponentIndex camera, float x, float y, Vec3& origin, Vec3& dir) = 0;

	virtual Frustum getCameraFrustum(ComponentIndex camera) const = 0;
	virtual void update(float dt) = 0;
	virtual float getTime() const = 0;
	virtual Engine& getEngine() const = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual Pose* getPose(ComponentIndex cmp) = 0;
	virtual ComponentIndex getActiveGlobalLight() = 0;
	virtual void setActiveGlobalLight(ComponentIndex cmp) = 0;
	virtual Vec4 getShadowmapCascades(ComponentIndex cmp) = 0;
	virtual void setShadowmapCascades(ComponentIndex cmp,
									  const Vec4& value) = 0;


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

	virtual void drawEmitterGizmo(ComponentIndex cmp) = 0;
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
	virtual void setParticleEmitterMaterialPath(ComponentIndex cmp, const char* path) = 0;
	virtual const char* getParticleEmitterMaterialPath(ComponentIndex cmp) = 0;
	virtual int getParticleEmitterPlaneCount(ComponentIndex cmp) = 0;
	virtual void addParticleEmitterPlane(ComponentIndex cmp, int index) = 0;
	virtual void removeParticleEmitterPlane(ComponentIndex cmp, int index) = 0;
	virtual Entity getParticleEmitterPlaneEntity(ComponentIndex cmp, int index) = 0;
	virtual void setParticleEmitterPlaneEntity(ComponentIndex cmp, int index, Entity entity) = 0;

	virtual DelegateList<void(ComponentIndex)>& renderableCreated() = 0;
	virtual DelegateList<void(ComponentIndex)>& renderableDestroyed() = 0;
	virtual void showRenderable(ComponentIndex cmp) = 0;
	virtual void hideRenderable(ComponentIndex cmp) = 0;
	virtual ComponentIndex getRenderableComponent(Entity entity) = 0;
	virtual Renderable* getRenderable(ComponentIndex cmp) = 0;
	virtual Renderable* getRenderables() = 0;
	virtual const char* getRenderablePath(ComponentIndex cmp) = 0;
	virtual void setRenderableLayer(ComponentIndex cmp,
									const int32& layer) = 0;
	virtual void setRenderablePath(ComponentIndex cmp, const char* path) = 0;
	virtual void getRenderableInfos(const Frustum& frustum,
		Array<RenderableMesh>& meshes,
		int64 layer_mask) = 0;
	virtual void getRenderableEntities(const Frustum& frustum,
		Array<Entity>& entities,
		int64 layer_mask) = 0;
	virtual Entity getRenderableEntity(ComponentIndex cmp) = 0;
	virtual ComponentIndex getFirstRenderable() = 0;
	virtual ComponentIndex getNextRenderable(ComponentIndex cmp) = 0;
	virtual Model* getRenderableModel(ComponentIndex cmp) = 0;

	virtual void getGrassInfos(const Frustum& frustum,
							   Array<GrassInfo>& infos,
							   int64 layer_mask,
							   ComponentIndex camera) = 0;
	virtual void forceGrassUpdate(ComponentIndex cmp) = 0;
	virtual void getTerrainInfos(Array<const TerrainInfo*>& infos,
		int64 layer_mask,
		const Vec3& camera_pos,
		LIFOAllocator& allocator) = 0;
	virtual float getTerrainHeightAt(ComponentIndex cmp, float x, float z) = 0;
	virtual Vec3 getTerrainNormalAt(ComponentIndex cmp, float x, float z) = 0;
	virtual void setTerrainMaterialPath(ComponentIndex cmp, const char* path) = 0;
	virtual const char* getTerrainMaterialPath(ComponentIndex cmp) = 0;
	virtual Material* getTerrainMaterial(ComponentIndex cmp) = 0;
	virtual void setTerrainXZScale(ComponentIndex cmp, float scale) = 0;
	virtual float getTerrainXZScale(ComponentIndex cmp) = 0;
	virtual void setTerrainYScale(ComponentIndex cmp, float scale) = 0;
	virtual float getTerrainYScale(ComponentIndex cmp) = 0;
	virtual void getTerrainSize(ComponentIndex cmp, float* width, float* height) = 0;
	virtual ComponentIndex getTerrainComponent(Entity entity) = 0;

	virtual bool isGrassEnabled() const = 0;
	virtual int getGrassDistance(ComponentIndex cmp) = 0;
	virtual void setGrassDistance(ComponentIndex cmp, int value) = 0;
	virtual void enableGrass(bool enabled) = 0;
	virtual void setGrassPath(ComponentIndex cmp, int index, const char* path) = 0;
	virtual const char* getGrassPath(ComponentIndex cmp, int index) = 0;
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
		Array<RenderableMesh>& infos,
		int64 layer_mask) = 0;
	virtual void getPointLightInfluencedGeometry(ComponentIndex light_cmp,
		const Frustum& frustum,
		Array<RenderableMesh>& infos,
		int64 layer_mask) = 0;
	virtual void setLightCastShadows(ComponentIndex cmp, bool cast_shadows) = 0;
	virtual bool getLightCastShadows(ComponentIndex cmp) = 0;
	virtual float getLightAttenuation(ComponentIndex cmp) = 0;
	virtual void setLightAttenuation(ComponentIndex cmp, float attenuation) = 0;
	virtual float getLightFOV(ComponentIndex cmp) = 0;
	virtual void setLightFOV(ComponentIndex cmp, float fov) = 0;
	virtual float getLightRange(ComponentIndex cmp) = 0;
	virtual void setLightRange(ComponentIndex cmp, float value) = 0;
	virtual void setPointLightIntensity(ComponentIndex cmp,
										float intensity) = 0;
	virtual void setGlobalLightIntensity(ComponentIndex cmp,
										 float intensity) = 0;
	virtual void setPointLightColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual void setGlobalLightColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual void setLightAmbientIntensity(ComponentIndex cmp,
										  float intensity) = 0;
	virtual void setLightAmbientColor(ComponentIndex cmp,
									  const Vec3& color) = 0;
	virtual void setFogDensity(ComponentIndex cmp, float density) = 0;
	virtual void setFogColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual float getPointLightIntensity(ComponentIndex cmp) = 0;
	virtual Entity getPointLightEntity(ComponentIndex cmp) const = 0;
	virtual Entity getGlobalLightEntity(ComponentIndex cmp) const = 0;
	virtual float getGlobalLightIntensity(ComponentIndex cmp) = 0;
	virtual Vec3 getPointLightColor(ComponentIndex cmp) = 0;
	virtual Vec3 getGlobalLightColor(ComponentIndex cmp) = 0;
	virtual float getLightAmbientIntensity(ComponentIndex cmp) = 0;
	virtual Vec3 getLightAmbientColor(ComponentIndex cmp) = 0;
	virtual float getFogDensity(ComponentIndex cmp) = 0;
	virtual float getFogBottom(ComponentIndex cmp) = 0;
	virtual float getFogHeight(ComponentIndex cmp) = 0;
	virtual void setFogBottom(ComponentIndex cmp, float value) = 0;
	virtual void setFogHeight(ComponentIndex cmp, float value) = 0;
	virtual Vec3 getFogColor(ComponentIndex cmp) = 0;
	virtual Vec3 getPointLightSpecularColor(ComponentIndex cmp) = 0;
	virtual void setPointLightSpecularColor(ComponentIndex cmp,
											const Vec3& color) = 0;

protected:
	virtual ~RenderScene() {}
};
}