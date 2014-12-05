#pragma once


#include "core/array.h"
#include "core/associative_array.h"
#include "core/matrix.h"
#include "core/resource.h"
#include "core/vec3.h"
#include "graphics/geometry.h"
#include "graphics/render_scene.h"
#include "universe/entity.h"


namespace Lumix
{


class Blob;
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
				void grassVertexCopyCallback(void* data, int instance_size, int copy_count);
				void grassIndexCopyCallback(void* data, int instance_size, int copy_count);
	
				Geometry* m_grass_geometry;
				Mesh* m_grass_mesh;
				Model* m_grass_model;
				Terrain& m_terrain;
				int32_t m_ground;
				int32_t m_density;
		};
		
		class GrassPatch
		{
			public:
				GrassPatch(IAllocator& allocator)
					: m_matrices(allocator)
				{ }

				Array<Matrix> m_matrices;
				GrassType* m_type;
		};

		class GrassQuad
		{
			public:
				GrassQuad(IAllocator& allocator)
					: m_patches(allocator)
				{}

				Array<GrassPatch> m_patches;
				float m_x;
				float m_z;
		};

	public:
		static const int GRASS_QUADS_COLUMNS = 5;
		static const int GRASS_QUADS_ROWS = 5;
		static const int GRASS_QUAD_SIZE = 10;

	public:
		Terrain(const Entity& entity, RenderScene& scene, IAllocator& allocator);
		~Terrain();

		void render(Renderer& renderer, PipelineInstance& pipeline, const Vec3& camera_pos);
		RayCastModelHit castRay(const Vec3& origin, const Vec3& dir);
		int64_t getLayerMask() const { return m_layer_mask; }
		void serialize(Blob& serializer);
		void deserialize(Blob& serializer, Universe& universe, RenderScene& scene, int index);
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
		void getGrassInfos(const Frustum& frustum, Array<GrassInfo>& infos, const Component& camera);
		void setBrush(const Vec3& position, float size) { m_brush_position = position; m_brush_size = size; }
		float getHeight(float x, float z);
		void getSize(float* width, float* height) const { ASSERT(width); ASSERT(height); *width = m_width * m_xz_scale; *height = m_height * m_xz_scale; }

	private: 
		Array<Terrain::GrassQuad*>& getQuads(const Component& camera);
		TerrainQuad* generateQuadTree(float size);
		void updateGrass(const Component& camera);
		void generateGeometry();
		void onMaterialLoaded(Resource::State, Resource::State new_state);
		float getHeight(int x, int z);
		void forceGrassUpdate();

	private:
		IAllocator& m_allocator;
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
		AssociativeArray<Component, Array<GrassQuad*> > m_grass_quads;
		AssociativeArray<Component, Vec3> m_last_camera_position; 
		Vec3 m_brush_position;
		float m_brush_size;
		bool m_force_grass_update;
};


} // namespace Lumix