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
class Texture;


class Terrain
{
	public:
		class GrassType
		{
			public:
				GrassType(Terrain& terrain);
				~GrassType();

				void grassLoaded(Resource::State, Resource::State);
				void grassVertexCopyCallback(Array<uint8_t>& data);
				void grassIndexCopyCallback(Array<int>& data);
	
				Geometry* m_grass_geometry;
				Mesh* m_grass_mesh;
				Model* m_grass_model;
				Terrain& m_terrain;
				int m_ground;
				int m_density;
		};
		
		class GrassPatch
		{
			public:
				Array<Matrix> m_matrices;
				GrassType* m_type;
		};

		class GrassQuad
		{
			public:
				Array<GrassPatch> m_patches;
				float m_x;
				float m_z;
		};

	public:
		static const int GRASS_QUADS_WIDTH = 5;
		static const int GRASS_QUADS_HEIGHT = 5;
		static const int GRASS_QUAD_SIZE = 10;

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
		void setGrassTypePath(int index, const Path& path);
		Path getGrassTypePath(int index);
		void setGrassTypeGround(int index, int ground);
		int getGrassTypeGround(int index);
		void setGrassTypeDensity(int index, int density);
		int getGrassTypeDensity(int index);
		void addGrassType(int index);
		void removeGrassType(int index);
		int getGrassTypeCount() const { return m_grass_types.size(); }
		void setMaterial(Material* material);
		void getGrassInfos(Array<GrassInfo>& infos, const Vec3& camera_position);
		void setBrush(const Vec3& position, float size) { m_brush_position = position; m_brush_size = size; }
		float getHeight(float x, float z);

	private: 
		void updateGrass(const Vec3& camera_position);
		void generateGeometry();
		void onMaterialLoaded(Resource::State, Resource::State new_state);
		float getHeight(int x, int z);
		void forceGrassUpdate();

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
		Texture* m_heightmap;
		Texture* m_splatmap;
		RenderScene& m_scene;
		Array<GrassType*> m_grass_types;
		Array<GrassQuad*> m_free_grass_quads;
		Array<GrassQuad*> m_grass_quads;
		Vec3 m_last_camera_position;
		Vec3 m_brush_position;
		float m_brush_size;
		bool m_force_grass_update;
};


} // namespace Lumix