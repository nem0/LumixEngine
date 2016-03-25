#pragma once


#include "core/array.h"
#include "core/associative_array.h"
#include "core/resource.h"
#include "core/vec.h"
#include <bgfx/bgfx.h>


namespace Lumix
{


struct AABB;
class Frustum;
struct GrassInfo;
class IAllocator;
class LIFOAllocator;
class Material;
struct Matrix;
struct Mesh;
class Model;
class OutputBlob;
struct RayCastModelHit;
class Renderer;
class RenderScene;
struct TerrainQuad;
struct TerrainInfo;
class Texture;
class Universe;


class Terrain
{
	public:
		class GrassType
		{
			public:
				explicit GrassType(Terrain& terrain);
				~GrassType();

				void grassLoaded(Resource::State, Resource::State);

				Model* m_grass_model;
				Terrain& m_terrain;
				int32 m_ground;
				int32 m_density;
				float m_distance;
		};

		class GrassPatch
		{
			public:
				explicit GrassPatch(IAllocator& allocator)
					: m_matrices(allocator)
				{ }

				Array<Matrix> m_matrices;
				GrassType* m_type;
		};

		class GrassQuad
		{
			public:
				explicit GrassQuad(IAllocator& allocator)
					: m_patches(allocator)
				{}

				Array<GrassPatch> m_patches;
				Vec3 pos;
				float radius;
		};

	public:
		Terrain(Renderer& renderer, Entity entity, RenderScene& scene, IAllocator& allocator);
		~Terrain();

		bgfx::VertexBufferHandle getVerticesHandle() const { return m_vertices_handle; }
		bgfx::IndexBufferHandle getIndicesHandle() const { return m_indices_handle; }
		Material* getMaterial() const { return m_material; }
		Texture* getDetailTexture() const { return m_detail_texture; }
		Texture* getSplatmap() const { return m_splatmap; }
		int64 getLayerMask() const { return m_layer_mask; }
		Entity getEntity() const { return m_entity; }
		float getRootSize() const;
		Vec3 getNormal(float x, float z);
		float getHeight(float x, float z) const;
		float getXZScale() const { return m_scale.x; }
		float getYScale() const { return m_scale.y; }
		Mesh* getMesh() { return m_mesh; }
		Path getGrassTypePath(int index);
		Vec3 getScale() const { return m_scale; }
		Vec2 getSize() const { return Vec2(m_width * m_scale.x, m_height * m_scale.z); }
		AABB getAABB() const;
		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }
		int getGrassTypeGround(int index) const;
		int getGrassTypeDensity(int index) const;
		float getGrassTypeDistance(int index) const;
		int getGrassTypeCount() const { return m_grass_types.size(); }

		void setXZScale(float scale) { m_scale.x = scale; m_scale.z = scale; }
		void setYScale(float scale) { m_scale.y = scale; }
		void setGrassTypePath(int index, const Path& path);
		void setGrassTypeGround(int index, int ground);
		void setGrassTypeDensity(int index, int density);
		void setGrassTypeDistance(int index, float value);
		void setMaterial(Material* material);

		void getInfos(Array<const TerrainInfo*>& infos, const Vec3& camera_pos, LIFOAllocator& allocator);
		void getGrassInfos(const Frustum& frustum, Array<GrassInfo>& infos, ComponentIndex camera);

		RayCastModelHit castRay(const Vec3& origin, const Vec3& dir);
		void serialize(OutputBlob& serializer);
		void deserialize(InputBlob& serializer, Universe& universe, RenderScene& scene, int index, int version);

		void addGrassType(int index);
		void removeGrassType(int index);
		void forceGrassUpdate();

	private: 
		Array<Terrain::GrassQuad*>& getQuads(ComponentIndex camera);
		TerrainQuad* generateQuadTree(float size);
		float getHeight(int x, int z) const;
		void updateGrass(ComponentIndex camera);
		void generateGrassTypeQuad(GrassPatch& patch,
								   const Matrix& terrain_matrix,
								   float quad_x,
								   float quad_z);
		void generateGeometry();
		void onMaterialLoaded(Resource::State, Resource::State new_state);

	private:
		IAllocator& m_allocator;
		bgfx::VertexBufferHandle m_vertices_handle;
		bgfx::IndexBufferHandle m_indices_handle;
		Mesh* m_mesh;
		TerrainQuad* m_root;
		int32 m_width;
		int32 m_height;
		int32 m_grass_distance;
		int64 m_layer_mask;
		Vec3 m_scale;
		Entity m_entity;
		Material* m_material;
		Texture* m_heightmap;
		Texture* m_splatmap;
		Texture* m_detail_texture;
		RenderScene& m_scene;
		Array<GrassType*> m_grass_types;
		Array<GrassQuad*> m_free_grass_quads;
		AssociativeArray<ComponentIndex, Array<GrassQuad*> > m_grass_quads;
		AssociativeArray<ComponentIndex, Vec3> m_last_camera_position;
		bool m_force_grass_update;
		Renderer& m_renderer;
};


} // namespace Lumix