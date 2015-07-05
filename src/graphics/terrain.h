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


class LIFOAllocator;
class Material;
class Mesh;
class OutputBlob;
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
		Terrain(Renderer& renderer, const Entity& entity, RenderScene& scene, IAllocator& allocator);
		~Terrain();

		Geometry* getGeometry() { return &m_geometry; }
		Material* getMaterial() const { return m_material; }
		int64_t getLayerMask() const { return m_layer_mask; }
		Entity getEntity() const { return m_entity; }
		float getRootSize() const;
		float getHeight(float x, float z);
		float getXZScale() const { return m_scale.x; }
		float getYScale() const { return m_scale.y; }
		float getBrushSize() const { return m_brush_size; }
		Mesh* getMesh() { return m_mesh; }
		Vec3 getBrushPosition() const { return m_brush_position; }
		Path getGrassTypePath(int index);
		Vec3 getScale() const { return m_scale; }
		void getSize(float* width, float* height) const { ASSERT(width); ASSERT(height); *width = m_width * m_scale.x; *height = m_height * m_scale.z; }
		int getGrassTypeGround(int index);
		int getGrassTypeDensity(int index);
		int getGrassTypeCount() const { return m_grass_types.size(); }

		void setXZScale(float scale) { m_scale.x = scale; m_scale.z = scale; }
		void setYScale(float scale) { m_scale.y = scale; }
		void setGrassTypePath(int index, const Path& path);
		void setGrassTypeGround(int index, int ground);
		void setGrassTypeDensity(int index, int density);
		void setMaterial(Material* material);
		void setBrush(const Vec3& position, float size) { m_brush_position = position; m_brush_size = size; }

		void getInfos(Array<const TerrainInfo*>& infos, const Vec3& camera_pos, LIFOAllocator& allocator);
		void getGrassInfos(const Frustum& frustum, Array<GrassInfo>& infos, const Component& camera);

		RayCastModelHit castRay(const Vec3& origin, const Vec3& dir);
		void serialize(OutputBlob& serializer);
		void deserialize(InputBlob& serializer, Universe& universe, RenderScene& scene, int index);

		void addGrassType(int index);
		void removeGrassType(int index);

	private: 
		Array<Terrain::GrassQuad*>& getQuads(const Component& camera);
		TerrainQuad* generateQuadTree(float size);
		float getHeight(int x, int z);
		void updateGrass(const Component& camera);
		void generateGeometry();
		void onMaterialLoaded(Resource::State, Resource::State new_state);
		void forceGrassUpdate();

	private:
		IAllocator& m_allocator;
		Mesh* m_mesh;
		TerrainQuad* m_root;
		Geometry m_geometry;
		int32_t m_width;
		int32_t m_height;
		int64_t m_layer_mask;
		Vec3 m_scale;
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
		Renderer& m_renderer;
};


} // namespace Lumix