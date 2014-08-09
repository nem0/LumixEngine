#pragma once


#include "core/array.h"
#include "core/matrix.h"
#include "core/resource.h"
#include "core/vec3.h"
#include "graphics/geometry.h"
#include "graphics/render_scene.h"
#include "universe/entity.h"


namespace Lumix
{


class ISerializer;
class Material;
class Mesh;
class PipelineInstance;
class Renderer;
class RenderScene;
struct TerrainQuad;


class Terrain
{
	public:
		class GrassQuad
		{
			public:
				Array<Matrix> m_matrices;
		};

	public:
		static const int GRASS_QUADS_WIDTH = 10;
		static const int GRASS_QUADS_HEIGHT = 10;

	public:
		Terrain(const Entity& entity, RenderScene& scene);
		~Terrain();

		void render(Renderer& renderer, PipelineInstance& pipeline, const Vec3& camera_pos);
		RayCastModelHit castRay(const Vec3& origin, const Vec3& dir);
		int64_t getLayerMask() const { return m_layer_mask; }
		void serialize(ISerializer& serializer);
		void deserialize(ISerializer& serializer, Universe& universe, RenderScene& scene, int index);
		void setXZScale(float scale) { m_xz_scale = scale; }
		float getXZScale() const { return m_xz_scale; }
		void setYScale(float scale) { m_y_scale = scale; }
		float getYScale() const { return m_y_scale; }
		Entity getEntity() const { return m_entity; }
		Material* getMaterial() const { return m_material; }
		void setGrass(const Path& path);
		void setMaterial(Material* material);
		void getGrassInfos(Array<GrassInfo>& infos, const Vec3& camera_position);

	private: 
		void updateGrass(const Vec3& camera_position);
		void generateGeometry();
		void onMaterialLoaded(Resource::State, Resource::State new_state);
		float getHeight(int x, int z);
		void grassLoaded(Resource::State, Resource::State);

	private:
		Mesh* m_mesh;
		TerrainQuad* m_root;
		Geometry m_geometry;
		int32_t m_width;
		int32_t m_height;
		int64_t m_layer_mask;
		float m_xz_scale;
		float m_y_scale;
		Entity m_entity;
		Material* m_material;
		Geometry* m_grass_geometry;
		Mesh* m_grass_mesh;
		RenderScene& m_scene;
		Model* m_grass_model;
		GrassQuad m_grass_quads[GRASS_QUADS_WIDTH][GRASS_QUADS_HEIGHT];
};


} // namespace Lumix