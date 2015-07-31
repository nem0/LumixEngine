#pragma once

#include "core/lumix.h"
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/matrix.h"
#include "core/string.h"
#include "engine/iplugin.h"
#include "graphics/ray_cast_model_hit.h"
#include "universe/component.h"


namespace Lumix
{

class Engine;
class Frustum;
class Geometry;
class LIFOAllocator;
class Material;
class Mesh;
class Model;
class ModelInstance;
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


struct RenderableMesh
{
	Mesh* m_mesh;
	const Pose* m_pose;
	const Matrix* m_matrix;
	const Model* m_model;
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
	uint32_t m_color;
	float m_life;
};


enum class RenderableType
{
	SKINNED_MESH,
	RIGID_MESH,
	GRASS,
	TERRAIN
};

class LUMIX_ENGINE_API RenderScene : public IScene
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

	virtual void applyCamera(ComponentIndex camera) = 0;
	virtual ComponentIndex getAppliedCamera() const = 0;
	virtual void update(float dt) = 0;
	virtual float getTime() const = 0;
	virtual Engine& getEngine() const = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual Pose& getPose(ComponentIndex cmp) = 0;
	virtual ComponentIndex getActiveGlobalLight() = 0;
	virtual void setActiveGlobalLight(ComponentIndex cmp) = 0;

	virtual void addDebugLine(const Vec3& from,
							  const Vec3& to,
							  const Vec3& color,
							  float life) = 0;
	virtual void addDebugLine(const Vec3& from,
							  const Vec3& to,
							  uint32_t color,
							  float life) = 0;
	virtual void addDebugCross(const Vec3& center,
							   float size,
							   const Vec3& color,
							   float life) = 0;
	virtual void addDebugCube(const Vec3& from,
							  const Vec3& max,
							  const Vec3& color,
							  float life) = 0;
	virtual void addDebugCircle(const Vec3& center,
								const Vec3& up,
								float radius,
								const Vec3& color,
								float life) = 0;
	virtual void addDebugSphere(const Vec3& center,
								float radius,
								const Vec3& color,
								float life) = 0;
	virtual void addDebugFrustum(const Vec3& position,
								 const Vec3& direction,
								 const Vec3& up,
								 float fov,
								 float ratio,
								 float near_distance,
								 float far_distance,
								 const Vec3& color,
								 float life) = 0;

	virtual void
	addDebugFrustum(const Frustum& frustum, const Vec3& color, float life) = 0;

	virtual void addDebugCylinder(const Vec3& position,
								  const Vec3& up,
								  float radius,
								  const Vec3& color,
								  float life) = 0;

	virtual const Array<DebugLine>& getDebugLines() const = 0;

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

	virtual DelegateList<void(ComponentIndex)>& renderableCreated() = 0;
	virtual DelegateList<void(ComponentIndex)>& renderableDestroyed() = 0;
	virtual void setRenderableIsAlwaysVisible(ComponentIndex cmp,
											  bool value) = 0;
	virtual bool isRenderableAlwaysVisible(ComponentIndex cmp) = 0;
	virtual void showRenderable(ComponentIndex cmp) = 0;
	virtual void hideRenderable(ComponentIndex cmp) = 0;
	virtual ComponentIndex getRenderableComponent(Entity entity) = 0;
	virtual const char* getRenderablePath(ComponentIndex cmp) = 0;
	virtual void setRenderableLayer(ComponentIndex cmp,
									const int32_t& layer) = 0;
	virtual void setRenderablePath(ComponentIndex cmp, const char* path) = 0;
	virtual void setRenderableScale(ComponentIndex cmp, float scale) = 0;
	virtual void getRenderableInfos(const Frustum& frustum,
									Array<const RenderableMesh*>& meshes,
									int64_t layer_mask) = 0;
	virtual void getRenderableMeshes(Array<RenderableMesh>& meshes,
									 int64_t layer_mask) = 0;
	virtual Entity getRenderableEntity(ComponentIndex cmp) = 0;
	virtual ComponentIndex getFirstRenderable() = 0;
	virtual ComponentIndex getNextRenderable(ComponentIndex cmp) = 0;
	virtual Model* getRenderableModel(ComponentIndex cmp) = 0;

	virtual void getGrassInfos(const Frustum& frustum,
							   Array<GrassInfo>& infos,
							   int64_t layer_mask) = 0;
	virtual void getTerrainInfos(Array<const TerrainInfo*>& infos,
								 int64_t layer_mask,
								 const Vec3& camera_pos,
								 LIFOAllocator& allocator) = 0;
	virtual float getTerrainHeightAt(ComponentIndex cmp, float x, float z) = 0;
	virtual void setTerrainMaterialPath(ComponentIndex cmp, const char* path) = 0;
	virtual const char* getTerrainMaterialPath(ComponentIndex cmp) = 0;
	virtual Material* getTerrainMaterial(ComponentIndex cmp) = 0;
	virtual void setTerrainXZScale(ComponentIndex cmp, float scale) = 0;
	virtual float getTerrainXZScale(ComponentIndex cmp) = 0;
	virtual void setTerrainYScale(ComponentIndex cmp, float scale) = 0;
	virtual float getTerrainYScale(ComponentIndex cmp) = 0;
	virtual void
	setTerrainBrush(ComponentIndex cmp, const Vec3& position, float size) = 0;
	virtual void
	getTerrainSize(ComponentIndex cmp, float* width, float* height) = 0;

	virtual void
	setGrassPath(ComponentIndex cmp, int index, const char* path) = 0;
	virtual const char* getGrassPath(ComponentIndex cmp, int index) = 0;
	virtual void setGrassGround(ComponentIndex cmp, int index, int ground) = 0;
	virtual int getGrassGround(ComponentIndex cmp, int index) = 0;
	virtual void
	setGrassDensity(ComponentIndex cmp, int index, int density) = 0;
	virtual int getGrassDensity(ComponentIndex cmp, int index) = 0;
	virtual int getGrassCount(ComponentIndex cmp) = 0;
	virtual void addGrass(ComponentIndex cmp, int index) = 0;
	virtual void removeGrass(ComponentIndex cmp, int index) = 0;

	virtual void getPointLights(const Frustum& frustum,
								Array<ComponentIndex>& lights) = 0;
	virtual void
	getPointLightInfluencedGeometry(ComponentIndex light_cmp,
									const Frustum& frustum,
									Array<const RenderableMesh*>& infos,
									int64_t layer_mask) = 0;
	virtual float getLightFOV(ComponentIndex cmp) = 0;
	virtual void setLightFOV(ComponentIndex cmp, float fov) = 0;
	virtual float getLightRange(ComponentIndex cmp) = 0;
	virtual void setLightRange(ComponentIndex cmp, float range) = 0;
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
	virtual Vec3 getFogColor(ComponentIndex cmp) = 0;
	virtual Frustum& getFrustum() = 0;
	virtual Vec3 getPointLightSpecularColor(ComponentIndex cmp) = 0;
	virtual void setPointLightSpecularColor(ComponentIndex cmp,
											const Vec3& color) = 0;

protected:
	virtual ~RenderScene() {}
};
}