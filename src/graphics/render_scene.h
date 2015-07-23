#pragma once

#include "core/lumix.h"
#include "core/array.h"
#include "core/string.h"
#include "engine/iplugin.h"
#include "graphics/ray_cast_model_hit.h"
#include "universe/component.h"

namespace Lumix
{

	class BitmapFont;
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
			static RenderScene* createInstance(Renderer& renderer, Engine& engine, Universe& universe, bool is_forward_rendered, IAllocator& allocator);
			static void destroyInstance(RenderScene* scene);

			virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const ComponentOld& ignore) = 0;
			virtual RayCastModelHit castRayTerrain(const ComponentNew& terrain, const Vec3& origin, const Vec3& dir) = 0;
			virtual void getRay(ComponentNew camera, float x, float y, Vec3& origin, Vec3& dir) = 0;
			virtual void applyCamera(ComponentNew camera) = 0;
			virtual ComponentOld getAppliedCamera() = 0;
			virtual void update(float dt) = 0;
			virtual float getTime() const = 0;
			virtual Engine& getEngine() const = 0;
			virtual IAllocator& getAllocator() = 0;

			virtual Pose& getPose(const ComponentNew& cmp) = 0;
			virtual ComponentOld getActiveGlobalLight() = 0;
			virtual void setActiveGlobalLight(const ComponentNew& cmp) = 0;

			virtual void addDebugLine(const Vec3& from, const Vec3& to, const Vec3& color, float life) = 0;
			virtual void addDebugLine(const Vec3& from, const Vec3& to, uint32_t color, float life) = 0;
			virtual void addDebugCross(const Vec3& center, float size, const Vec3& color, float life) = 0;
			virtual void addDebugCube(const Vec3& from, const Vec3& max, const Vec3& color, float life) = 0;
			virtual void addDebugCircle(const Vec3& center, const Vec3& up, float radius, const Vec3& color, float life) = 0;
			virtual void addDebugSphere(const Vec3& center, float radius, const Vec3& color, float life) = 0;
			virtual void addDebugFrustum(const Vec3& position, const Vec3& direction, const Vec3& up, float fov, float ratio, float near_distance, float far_distance, const Vec3& color, float life) = 0;
			virtual void addDebugFrustum(const Frustum& frustum, const Vec3& color, float life) = 0;
			virtual void addDebugCylinder(const Vec3& position, const Vec3& up, float radius, const Vec3& color, float life) = 0;

			virtual const Array<DebugLine>& getDebugLines() const = 0;
			
			virtual ComponentOld getCameraInSlot(const char* slot) = 0;
			virtual float getCameraFOV(ComponentNew camera) = 0;
			virtual void setCameraFOV(ComponentNew camera, float fov) = 0;
			virtual void setCameraFarPlane(ComponentNew camera, float far) = 0;
			virtual void setCameraNearPlane(ComponentNew camera, float near) = 0;
			virtual float getCameraFarPlane(ComponentNew camera) = 0;
			virtual float getCameraNearPlane(ComponentNew camera) = 0;
			virtual float getCameraWidth(ComponentNew camera) = 0;
			virtual float getCameraHeight(ComponentNew camera) = 0;
			virtual void setCameraSlot(ComponentNew camera, const string& slot) = 0;
			virtual void getCameraSlot(ComponentNew camera, string& slot) = 0;
			virtual void setCameraSize(ComponentNew camera, int w, int h) = 0;
			
			virtual void setRenderableIsAlwaysVisible(ComponentOld cmp, bool value) = 0;
			virtual bool isRenderableAlwaysVisible(ComponentOld cmp) = 0;
			virtual void showRenderable(ComponentOld cmp) = 0;
			virtual void hideRenderable(ComponentOld cmp) = 0;
			virtual ComponentOld getRenderableComponent(Entity entity) = 0;
			virtual void getRenderablePath(ComponentOld cmp, string& path) = 0;
			virtual void setRenderableLayer(ComponentOld cmp, const int32_t& layer) = 0;
			virtual void setRenderablePath(ComponentOld cmp, const string& path) = 0;
			virtual void setRenderablePath(int cmp_index, const string& path) = 0;
			virtual void setRenderableScale(ComponentOld cmp, float scale) = 0;
			virtual void getRenderableInfos(const Frustum& frustum, Array<const RenderableMesh*>& meshes, int64_t layer_mask) = 0;
			virtual void getRenderableMeshes(Array<RenderableMesh>& meshes, int64_t layer_mask) = 0;
			virtual ComponentOld getFirstRenderable() = 0;
			virtual ComponentOld getNextRenderable(const ComponentOld& cmp) = 0;
			virtual Model* getRenderableModel(ComponentOld cmp) = 0;

			virtual void getGrassInfos(const Frustum& frustum, Array<GrassInfo>& infos, int64_t layer_mask) = 0;
			virtual void getTerrainInfos(Array<const TerrainInfo*>& infos, int64_t layer_mask, const Vec3& camera_pos, LIFOAllocator& allocator) = 0;
			virtual float getTerrainHeightAt(ComponentOld cmp, float x, float z) = 0;
			virtual void setTerrainMaterial(ComponentOld cmp, const string& path) = 0;
			virtual void getTerrainMaterial(ComponentOld cmp, string& path) = 0;
			virtual void setTerrainXZScale(ComponentOld cmp, float scale) = 0;
			virtual float getTerrainXZScale(ComponentOld cmp) = 0;
			virtual void setTerrainYScale(ComponentOld cmp, float scale) = 0;
			virtual float getTerrainYScale(ComponentOld cmp) = 0;
			virtual void setTerrainBrush(ComponentOld cmp, const Vec3& position, float size) = 0;
			virtual void getTerrainSize(ComponentOld cmp, float* width, float* height) = 0;

			virtual void setGrass(ComponentOld cmp, int index, const string& path) = 0;
			virtual void getGrass(ComponentOld cmp, int index, string& path) = 0;
			virtual void setGrassGround(ComponentOld cmp, int index, int ground) = 0;
			virtual int getGrassGround(ComponentOld cmp, int index) = 0;
			virtual void setGrassDensity(ComponentOld cmp, int index, int density) = 0;
			virtual int getGrassDensity(ComponentOld cmp, int index) = 0;
			virtual int getGrassCount(ComponentOld cmp) = 0;
			virtual void addGrass(ComponentOld cmp, int index) = 0;
			virtual void removeGrass(ComponentOld cmp, int index) = 0;

			virtual Entity getPointLightEntity(ComponentOld cmp) = 0;
			virtual void getPointLights(const Frustum& frustum, Array<ComponentOld>& lights) = 0;
			virtual void getPointLightInfluencedGeometry(const ComponentOld& light_cmp, const Frustum& frustum, Array<const RenderableMesh*>& infos, int64_t layer_mask) = 0;
			virtual float getLightFOV(ComponentOld cmp) = 0;
			virtual void setLightFOV(ComponentOld cmp, float fov) = 0;
			virtual float getLightRange(ComponentOld cmp) = 0;
			virtual void setLightRange(ComponentOld cmp, float range) = 0;
			virtual void setPointLightIntensity(ComponentOld cmp, float intensity) = 0;
			virtual void setGlobalLightIntensity(ComponentOld cmp, float intensity) = 0;
			virtual void setPointLightColor(ComponentOld cmp, const Vec3& color) = 0;
			virtual void setGlobalLightColor(ComponentOld cmp, const Vec3& color) = 0;
			virtual void setLightAmbientIntensity(ComponentOld cmp, float intensity) = 0;
			virtual void setLightAmbientColor(ComponentOld cmp, const Vec3& color) = 0;
			virtual void setFogDensity(ComponentOld cmp, float density) = 0;
			virtual void setFogColor(ComponentOld cmp, const Vec3& color) = 0;
			virtual float getPointLightIntensity(ComponentOld cmp) = 0;
			virtual float getGlobalLightIntensity(ComponentOld cmp) = 0;
			virtual Vec3 getPointLightColor(ComponentOld cmp) = 0;
			virtual Vec3 getGlobalLightColor(ComponentOld cmp) = 0;
			virtual float getLightAmbientIntensity(ComponentOld cmp) = 0;
			virtual Vec3 getLightAmbientColor(ComponentOld cmp) = 0;
			virtual float getFogDensity(ComponentOld cmp) = 0;
			virtual Vec3 getFogColor(ComponentOld cmp) = 0;
			virtual Frustum& getFrustum() = 0;
			virtual Vec3 getPointLightSpecularColor(ComponentOld cmp) = 0;
			virtual void setPointLightSpecularColor(ComponentOld cmp, const Vec3& color) = 0;

		protected:
			virtual ~RenderScene() {}
	};


}