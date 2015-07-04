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
	class IRenderDevice;
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
		const Mesh* m_mesh;
		const Pose* m_pose;
		const Matrix* m_matrix;
		const Model* m_model;
	};

	struct RenderableInfo
	{
		int64_t m_key;
		const void* m_data;
		int32_t m_type;
	};

	struct GrassInfo
	{
		Geometry* m_geometry;
		Mesh* m_mesh;
		const Matrix* m_matrices;
		int m_matrix_count;
		int m_mesh_copy_count;
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

			virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Component& ignore) = 0;
			virtual RayCastModelHit castRayTerrain(const Component& terrain, const Vec3& origin, const Vec3& dir) = 0;
			virtual void getRay(Component camera, float x, float y, Vec3& origin, Vec3& dir) = 0;
			virtual void applyCamera(Component camera) = 0;
			virtual Component getAppliedCamera() = 0;
			virtual void update(float dt) = 0;
			virtual float getTime() const = 0;
			virtual Engine& getEngine() const = 0;
			virtual IAllocator& getAllocator() = 0;

			virtual Pose& getPose(const Component& cmp) = 0;
			virtual Component getActiveGlobalLight() = 0;
			virtual void setActiveGlobalLight(const Component& cmp) = 0;

			virtual int addDebugText(const char* text, int x, int y) = 0;
			virtual void setDebugText(int id, const char* text) = 0;
			virtual Geometry& getDebugTextGeometry() = 0;
			virtual Mesh& getDebugTextMesh() = 0;
			virtual const char* getDebugText(int index) = 0;

			virtual BitmapFont* getDebugTextFont() = 0;
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
			
			virtual Component getCameraInSlot(const char* slot) = 0;
			virtual float getCameraFOV(Component camera) = 0;
			virtual void setCameraFOV(Component camera, float fov) = 0;
			virtual void setCameraFarPlane(Component camera, float far) = 0;
			virtual void setCameraNearPlane(Component camera, float near) = 0;
			virtual float getCameraFarPlane(Component camera) = 0;
			virtual float getCameraNearPlane(Component camera) = 0;
			virtual float getCameraWidth(Component camera) = 0;
			virtual float getCameraHeight(Component camera) = 0;
			virtual void setCameraSlot(Component camera, const string& slot) = 0;
			virtual void getCameraSlot(Component camera, string& slot) = 0;
			virtual void setCameraSize(Component camera, int w, int h) = 0;
			
			virtual void setRenderableIsAlwaysVisible(Component cmp, bool value) = 0;
			virtual bool isRenderableAlwaysVisible(Component cmp) = 0;
			virtual void showRenderable(Component cmp) = 0;
			virtual void hideRenderable(Component cmp) = 0;
			virtual Component getRenderable(Entity entity) = 0;
			virtual void getRenderablePath(Component cmp, string& path) = 0;
			virtual void setRenderableLayer(Component cmp, const int32_t& layer) = 0;
			virtual void setRenderablePath(Component cmp, const string& path) = 0;
			virtual void setRenderableScale(Component cmp, float scale) = 0;
			virtual void getRenderableInfos(const Frustum& frustum, Array<RenderableInfo>& infos, int64_t layer_mask) = 0;
			virtual void getRenderableMeshes(Array<RenderableMesh>& meshes, int64_t layer_mask) = 0;
			virtual Component getFirstRenderable() = 0;
			virtual Component getNextRenderable(const Component& cmp) = 0;
			virtual Model* getRenderableModel(Component cmp) = 0;

			virtual void getGrassInfos(const Frustum& frustum, Array<RenderableInfo>& infos, int64_t layer_mask) = 0;
			virtual void getTerrainInfos(Array<RenderableInfo>& infos, int64_t layer_mask, const Vec3& camera_pos, LIFOAllocator& allocator) = 0;
			virtual float getTerrainHeightAt(Component cmp, float x, float z) = 0;
			virtual void setTerrainMaterial(Component cmp, const string& path) = 0;
			virtual void getTerrainMaterial(Component cmp, string& path) = 0;
			virtual void setTerrainXZScale(Component cmp, float scale) = 0;
			virtual float getTerrainXZScale(Component cmp) = 0;
			virtual void setTerrainYScale(Component cmp, float scale) = 0;
			virtual float getTerrainYScale(Component cmp) = 0;
			virtual void setTerrainBrush(Component cmp, const Vec3& position, float size) = 0;
			virtual void getTerrainSize(Component cmp, float* width, float* height) = 0;

			virtual void setGrass(Component cmp, int index, const string& path) = 0;
			virtual void getGrass(Component cmp, int index, string& path) = 0;
			virtual void setGrassGround(Component cmp, int index, int ground) = 0;
			virtual int getGrassGround(Component cmp, int index) = 0;
			virtual void setGrassDensity(Component cmp, int index, int density) = 0;
			virtual int getGrassDensity(Component cmp, int index) = 0;
			virtual int getGrassCount(Component cmp) = 0;
			virtual void addGrass(Component cmp, int index) = 0;
			virtual void removeGrass(Component cmp, int index) = 0;

			virtual Entity getPointLightEntity(Component cmp) = 0;
			virtual void getPointLights(const Frustum& frustum, Array<Component>& lights) = 0;
			virtual void getPointLightInfluencedGeometry(const Component& light_cmp, const Frustum& frustum, Array<RenderableInfo>& infos, int64_t layer_mask) = 0;
			virtual float getLightFOV(Component cmp) = 0;
			virtual void setLightFOV(Component cmp, float fov) = 0;
			virtual float getLightRange(Component cmp) = 0;
			virtual void setLightRange(Component cmp, float range) = 0;
			virtual void setPointLightIntensity(Component cmp, float intensity) = 0;
			virtual void setGlobalLightIntensity(Component cmp, float intensity) = 0;
			virtual void setPointLightColor(Component cmp, const Vec4& color) = 0;
			virtual void setGlobalLightColor(Component cmp, const Vec4& color) = 0;
			virtual void setLightAmbientIntensity(Component cmp, float intensity) = 0;
			virtual void setLightAmbientColor(Component cmp, const Vec4& color) = 0;
			virtual void setFogDensity(Component cmp, float density) = 0;
			virtual void setFogColor(Component cmp, const Vec4& color) = 0;
			virtual float getPointLightIntensity(Component cmp) = 0;
			virtual float getGlobalLightIntensity(Component cmp) = 0;
			virtual Vec4 getPointLightColor(Component cmp) = 0;
			virtual Vec4 getGlobalLightColor(Component cmp) = 0;
			virtual float getLightAmbientIntensity(Component cmp) = 0;
			virtual Vec4 getLightAmbientColor(Component cmp) = 0;
			virtual float getFogDensity(Component cmp) = 0;
			virtual Vec4 getFogColor(Component cmp) = 0;
			virtual Frustum& getFrustum() = 0;

		protected:
			virtual ~RenderScene() {}
	};


}