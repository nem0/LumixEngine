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
		Terrain(const Entity& entity);
		~Terrain();

		void render(Renderer& renderer, PipelineInstance& pipeline, const Vec3& camera_pos);
		int64_t getLayerMask() const { return m_layer_mask; }
		void serialize(ISerializer& serializer);
		void deserialize(ISerializer& serializer, Universe& universe, RenderScene& scene, int index);
		void setXZScale(float scale) { m_xz_scale = scale; }
		float getXZScale() const { return m_xz_scale; }
		void setYScale(float scale) { m_y_scale = scale; }
		float getYScale() const { return m_y_scale; }
		Entity getEntity() const { return m_entity; }
		Material* getMaterial() const { return m_material; }
		void setMaterial(Material* material);

	private: 
		void generateGeometry();
		void onMaterialLoaded(Resource::State, Resource::State new_state);

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
};


} // namespace Lumix