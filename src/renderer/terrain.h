#pragma once


#include "engine/array.h"
#include "engine/math.h"
#include "engine/resource.h"
#include "gpu/gpu.h"


namespace Lumix
{


struct AABB;
struct Frustum;
struct IAllocator;
struct Material;
struct Mesh;
struct Model;
struct RayCastModelHit;
struct Renderer;
struct RenderScene;
struct ShiftedFrustum;
struct TerrainQuad;
struct TerrainInfo;
struct Texture;
struct Universe;


struct Terrain
{
	public:
		enum { TEXTURES_COUNT = 6 };

		struct GrassType
		{
			explicit GrassType(Terrain& terrain);
			~GrassType();

			Model* m_grass_model;
			Terrain& m_terrain;
			i32 m_density;
			float m_distance;
			int m_idx;
			enum class RotationMode : int
			{
				Y_UP,
				ALL_RANDOM,
				ALIGN_WITH_NORMAL,

				COUNT,
			};
			RotationMode m_rotation_mode = RotationMode::Y_UP;
		};

		struct GrassPatch
		{
			struct InstanceData
			{
				Quat rot;
				Vec4 pos_scale;
				Vec4 normal;
			};
			explicit GrassPatch(IAllocator& allocator)
				: instance_data(allocator)
			{ }

			Array<InstanceData> instance_data;
			GrassType* m_type;
		};

		struct GrassQuad
		{
			explicit GrassQuad(IAllocator& allocator)
				: m_patches(allocator)
			{}

			Array<GrassPatch> m_patches;
			Vec3 pos;
			float radius;
		};

	public:
		Terrain(Renderer& renderer, EntityPtr entity, RenderScene& scene, IAllocator& allocator);
		~Terrain();

		Material* getMaterial() const { return m_material; }
		Texture* getAlbedomap() const { return m_albedomap; }
		Texture* getSplatmap() const { return m_splatmap; }
		Texture* getHeightmap() const { return m_heightmap; }
		i64 getLayerMask() const { return m_layer_mask; }
		EntityRef getEntity() const { return m_entity; }
		Vec3 getNormal(float x, float z);
		float getHeight(float x, float z) const;
		float getXZScale() const { return m_scale.x; }
		float getYScale() const { return m_scale.y; }
		Path getGrassTypePath(int index);
		Vec3 getScale() const { return m_scale; }
		Vec2 getSize() const { return Vec2(m_width * m_scale.x, m_height * m_scale.z); }
		AABB getAABB() const;
		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }
		int getGrassTypeDensity(int index) const;
		float getGrassTypeDistance(int index) const;
		GrassType::RotationMode getGrassTypeRotationMode(int index) const;
		int getGrassTypeCount() const { return m_grass_types.size(); }

		float getHeight(int x, int z) const;
		void setHeight(int x, int z, float height);
		void setXZScale(float scale);
		void setYScale(float scale);
		void setGrassTypePath(int index, const Path& path);
		void setGrassTypeDensity(int index, int density);
		void setGrassTypeDistance(int index, float value);
		void setGrassTypeRotationMode(int index, GrassType::RotationMode mode);
		void setMaterial(Material* material);

		void getInfos(Array<TerrainInfo>& infos, const ShiftedFrustum& frustum, const DVec3& lod_ref_point);

		RayCastModelHit castRay(const DVec3& origin, const Vec3& dir);
		void serialize(IOutputStream& serializer);
		void deserialize(EntityRef entity, IInputStream& serializer, Universe& universe, RenderScene& scene);

		void addGrassType(int index);
		void removeGrassType(int index);
		void forceGrassUpdate();
		void updateGrass(int view, const DVec3& position);

	private: 
		Array<Terrain::GrassQuad*>& getQuads(int view);
		void generateGrassTypeQuad(GrassPatch& patch, const RigidTransform& terrain_tr, const Vec2& quad_pos_hm_space);
		void onMaterialLoaded(Resource::State, Resource::State new_state, Resource&);
		void grassLoaded(Resource::State, Resource::State, Resource&);

	public:
		IAllocator& m_allocator;
		i32 m_width;
		i32 m_height;
		i64 m_layer_mask;
		Vec3 m_scale;
		EntityRef m_entity;
		Material* m_material;
		Texture* m_heightmap;
		Texture* m_splatmap;
		Texture* m_albedomap;
		RenderScene& m_scene;
		Array<GrassType> m_grass_types;
		Array<Array<GrassQuad*> > m_grass_quads;
		Array<DVec3> m_last_camera_position;
		bool m_force_grass_update;
		Renderer& m_renderer;
};


} // namespace Lumix