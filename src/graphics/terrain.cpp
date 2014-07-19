#include "terrain.h"
#include "core/iserializer.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "engine/engine.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/pipeline.h"
#include "graphics/render_scene.h"
#include "graphics/shader.h"
#include "graphics/texture.h"


namespace Lumix
{
	
	static const int GRID_SIZE = 16;
	static const uint32_t TERRAIN_HASH = crc32("terrain");

	struct Sample
	{
		Vec3 pos;
		float u, v;
	};

	struct TerrainQuad
	{
		enum ChildType
		{
			TOP_LEFT,
			TOP_RIGHT,
			BOTTOM_LEFT,
			BOTTOM_RIGHT,
			CHILD_COUNT
		};

		TerrainQuad()
		{
			for (int i = 0; i < CHILD_COUNT; ++i)
			{
				m_children[i] = NULL;
			}
		}

		~TerrainQuad()
		{
			for (int i = 0; i < CHILD_COUNT; ++i)
			{
				LUMIX_DELETE(m_children[i]);
			}
		}

		void createChildren()
		{
			if (m_lod < 8 && m_size > 16)
			{
				for (int i = 0; i < CHILD_COUNT; ++i)
				{
					m_children[i] = LUMIX_NEW(TerrainQuad);
					m_children[i]->m_lod = m_lod + 1;
					m_children[i]->m_size = m_size / 2;
				}
				m_children[TOP_LEFT]->m_min = m_min;
				m_children[TOP_RIGHT]->m_min.set(m_min.x + m_size / 2, 0, m_min.z);
				m_children[BOTTOM_LEFT]->m_min.set(m_min.x, 0, m_min.z + m_size / 2);
				m_children[BOTTOM_RIGHT]->m_min.set(m_min.x + m_size / 2, 0, m_min.z + m_size / 2);
				for (int i = 0; i < CHILD_COUNT; ++i)
				{
					m_children[i]->createChildren();
				}
			}
		}

		float getDistance(const Vec3& camera_pos)
		{
			Vec3 _max(m_min.x + m_size, m_min.y, m_min.z + m_size);
			float dist = 0;
			if (camera_pos.x < m_min.x)
			{
				float d = m_min.x - camera_pos.x;
				dist += d*d;
			}
			if (camera_pos.x > _max.x)
			{
				float d = _max.x - camera_pos.x;
				dist += d*d;
			}
			if (camera_pos.z < m_min.z)
			{
				float d = m_min.z - camera_pos.z;
				dist += d*d;
			}
			if (camera_pos.z > _max.z)
			{
				float d = _max.z - camera_pos.z;
				dist += d*d;
			}
			return sqrt(dist);
		}

		static float getRadiusInner(float size)
		{
			float lower_level_size = size / 2;
			float lower_level_diagonal = sqrt(2 * size / 2 * size / 2);
			return getRadiusOuter(lower_level_size) + lower_level_diagonal;
		}

		static float getRadiusOuter(float size)
		{
			return (size > 17 ? 2 : 1) * sqrt(2 * size*size) + size * 0.25f;
		}


		bool render(Mesh* mesh, Geometry& geometry, const Vec3& camera_pos, RenderScene& scene)
		{
			float dist = getDistance(camera_pos);
			float r = getRadiusOuter(m_size);
			if (dist > r && m_lod > 1)
			{
				return false;
			}
			Vec3 morph_const(r, getRadiusInner(m_size), 0);
			Shader& shader = *mesh->getMaterial()->getShader();
			for (int i = 0; i < CHILD_COUNT; ++i)
			{
				if (!m_children[i] || !m_children[i]->render(mesh, geometry, camera_pos, scene))
				{
					shader.setUniform("morph_const", morph_const);
					shader.setUniform("quad_size", m_size);
					shader.setUniform("quad_min", m_min);
					geometry.draw(mesh->getCount() / 4 * i, mesh->getCount() / 4, shader);
				}
			}
			return true;
		}

		TerrainQuad* m_children[CHILD_COUNT];
		Vec3 m_min;
		float m_size;
		int m_lod;
		float m_xz_scale;
	};


	Terrain::Terrain(const Entity& entity)
		: m_mesh(NULL)
		, m_material(NULL)
		, m_root(NULL)
		, m_width(0)
		, m_height(0)
		, m_layer_mask(1)
		, m_y_scale(1)
		, m_xz_scale(1)
		, m_entity(entity)
	{
		generateGeometry();
	}

	Terrain::~Terrain()
	{
		LUMIX_DELETE(m_mesh);
		LUMIX_DELETE(m_root);
		if (m_material)
		{
			m_material->getObserverCb().unbind<Terrain, &Terrain::onMaterialLoaded>(this);
			m_material->getResourceManager().get(ResourceManager::MATERIAL)->unload(*m_material);
		}
	}


	void Terrain::setMaterial(Material* material)
	{
		if (material != m_material)
		{
			if (m_material)
			{
				m_material->getResourceManager().get(ResourceManager::MATERIAL)->unload(*m_material);
				m_material->getObserverCb().unbind<Terrain, &Terrain::onMaterialLoaded>(this);
			}
			m_material = material;
			if (m_mesh)
			{
				m_mesh->setMaterial(m_material);
				m_material->getObserverCb().bind<Terrain, &Terrain::onMaterialLoaded>(this);
			}
		}
		else
		{
			material->getResourceManager().get(ResourceManager::MATERIAL)->unload(*material);
		}
	}

	void Terrain::deserialize(ISerializer& serializer, Universe& universe, RenderScene& scene, int index)
	{
		serializer.deserializeArrayItem(m_entity.index);
		m_entity.universe = &universe;
		serializer.deserializeArrayItem(m_layer_mask);
		char path[LUMIX_MAX_PATH];
		serializer.deserializeArrayItem(path, LUMIX_MAX_PATH);
		setMaterial(static_cast<Material*>(scene.getEngine().getResourceManager().get(ResourceManager::MATERIAL)->load(path)));
		serializer.deserializeArrayItem(m_xz_scale);
		serializer.deserializeArrayItem(m_y_scale);
		universe.addComponent(m_entity, TERRAIN_HASH, &scene, index);
	}


	void Terrain::serialize(ISerializer& serializer)
	{
		serializer.serializeArrayItem(m_entity.index);
		serializer.serializeArrayItem(m_layer_mask);
		serializer.serializeArrayItem(m_material->getPath().c_str());
		serializer.serializeArrayItem(m_xz_scale);
		serializer.serializeArrayItem(m_y_scale);
	}


	void Terrain::render(Renderer& renderer, PipelineInstance& pipeline, const Vec3& camera_pos)
	{
		if (m_root)
		{
			m_material->apply(renderer, pipeline);
			m_mesh->getMaterial()->getShader()->setUniform("map_size", m_root->m_size);
			m_mesh->getMaterial()->getShader()->setUniform("camera_pos", camera_pos);
			m_root->render(m_mesh, m_geometry, camera_pos, *pipeline.getScene());
		}
	}

	static TerrainQuad* generateQuadTree(float size)
	{
		TerrainQuad* root = LUMIX_NEW(TerrainQuad);
		root->m_lod = 1;
		root->m_min.set(0, 0, 0);
		root->m_size = size;
		root->createChildren();
		return root;
	}


	static void generateSubgrid(Array<Sample>& samples, Array<int32_t>& indices, int& indices_offset, int start_x, int start_y)
	{
		for (int j = start_y; j < start_y + 8; ++j)
		{
			for (int i = start_x; i < start_x + 8; ++i)
			{
				int idx = 4 * (i + j * GRID_SIZE);
				samples[idx].pos.set((float)(i) / GRID_SIZE, 0, (float)(j) / GRID_SIZE);
				samples[idx + 1].pos.set((float)(i + 1) / GRID_SIZE, 0, (float)(j) / GRID_SIZE);
				samples[idx + 2].pos.set((float)(i + 1) / GRID_SIZE, 0, (float)(j + 1) / GRID_SIZE);
				samples[idx + 3].pos.set((float)(i) / GRID_SIZE, 0, (float)(j + 1) / GRID_SIZE);
				samples[idx].u = 0;
				samples[idx].v = 0;
				samples[idx + 1].u = 1;
				samples[idx + 1].v = 0;
				samples[idx + 2].u = 1;
				samples[idx + 2].v = 1;
				samples[idx + 3].u = 0;
				samples[idx + 3].v = 1;

				indices[indices_offset] = idx;
				indices[indices_offset + 1] = idx + 3;
				indices[indices_offset + 2] = idx + 2;
				indices[indices_offset + 3] = idx;
				indices[indices_offset + 4] = idx + 2;
				indices[indices_offset + 5] = idx + 1;
				indices_offset += 6;
			}
		}
	}

	void Terrain::generateGeometry()
	{
		LUMIX_DELETE(m_mesh);
		m_mesh = NULL;
		Array<Sample> points;
		points.resize(GRID_SIZE * GRID_SIZE * 4);
		Array<int32_t> indices;
		indices.resize(GRID_SIZE * GRID_SIZE * 6);
		int indices_offset = 0;
		generateSubgrid(points, indices, indices_offset, 0, 0);
		generateSubgrid(points, indices, indices_offset, 8, 0);
		generateSubgrid(points, indices, indices_offset, 0, 8);
		generateSubgrid(points, indices, indices_offset, 8, 8);

		VertexDef vertex_def;
		vertex_def.parse("pt", 2);
		m_geometry.copy((const uint8_t*)&points[0], sizeof(points[0]) * points.size(), indices, vertex_def);
		m_mesh = LUMIX_NEW(Mesh)(m_material, 0, indices.size(), "terrain");
	}

	void Terrain::onMaterialLoaded(Resource::State, Resource::State new_state)
	{
		PROFILE_FUNCTION();
		if (new_state == Resource::State::READY)
		{
			m_width = m_material->getTexture(0)->getWidth();
			m_height = m_material->getTexture(0)->getHeight();
			LUMIX_DELETE(m_root);
			m_root = generateQuadTree((float)m_width);
		}
	}


} // namespace Lumix