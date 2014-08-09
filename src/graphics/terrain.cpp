#include "terrain.h"
#include "core/iserializer.h"
#include "core/math_utils.h"
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
	static const int COPY_COUNT = 50;
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
			if (m_lod < 16 && m_size > 16)
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


	Terrain::Terrain(const Entity& entity, RenderScene& scene)
		: m_mesh(NULL)
		, m_material(NULL)
		, m_root(NULL)
		, m_width(0)
		, m_height(0)
		, m_layer_mask(1)
		, m_y_scale(1)
		, m_xz_scale(1)
		, m_entity(entity)
		, m_grass_geometry(NULL)
		, m_grass_mesh(NULL)
		, m_scene(scene)
		, m_grass_model(NULL)
	{
		generateGeometry();
	}

	Terrain::~Terrain()
	{
		setMaterial(NULL);
		LUMIX_DELETE(m_mesh);
		LUMIX_DELETE(m_root);
		if (m_grass_model)
		{
			m_grass_model->getResourceManager().get(ResourceManager::MODEL)->unload(*m_grass_model);
			m_grass_model->getObserverCb().unbind<Terrain, &Terrain::grassLoaded>(this);
			LUMIX_DELETE(m_grass_mesh);
			LUMIX_DELETE(m_grass_geometry);
		}
	}


	void Terrain::setGrass(const Path& path)
	{
		if (m_grass_model)
		{
			m_grass_model->getResourceManager().get(ResourceManager::MODEL)->unload(*m_grass_model);
			m_grass_model->getObserverCb().unbind<Terrain, &Terrain::grassLoaded>(this);
			LUMIX_DELETE(m_grass_mesh);
			LUMIX_DELETE(m_grass_geometry);
			m_grass_mesh = NULL;
			m_grass_geometry = NULL;
		}
		m_grass_model = static_cast<Model*>(m_scene.getEngine().getResourceManager().get(ResourceManager::MODEL)->load(path));
		m_grass_model->getObserverCb().bind<Terrain, &Terrain::grassLoaded>(this);
	}

	void Terrain::updateGrass(const Vec3& camera_position)
	{
		float cx = (int)((camera_position.x + 5) / 10) * 10.0f;
		float cy = (int)((camera_position.z + 5) / 10) * 10.0f;
		for (int j = 0; j < GRASS_QUADS_HEIGHT; ++j)
		{
			for (int i = 0; i < GRASS_QUADS_WIDTH; ++i)
			{
				m_grass_quads[i][j].m_matrices.resize(10 * 10);
				float x = cx - (i - 5) * 10;
				float y = cy - (j - 5) * 10;
				if (x > -1 && y > -1)
				{
					for (int a = 0; a < 10; ++a)
					{
						for (int b = 0; b < 10; ++b)
						{
							m_grass_quads[i][j].m_matrices[a + b * 10] = Matrix::IDENTITY;
							m_grass_quads[i][j].m_matrices[a + b * 10].setTranslation(Vec3(x + a, getHeight(x + a, y + b), y + b));
						}
					}
				}
			}
		}

/*		for (int i = 0; i < COPY_COUNT; ++i)
		{
			m_grass_matrices[i] = Matrix::IDENTITY;
			if (camera_position.z > 0 && camera_position.x > 0)
			{
				Vec3 pos((int)(camera_position.x + i), getHeight(camera_position.x + i, camera_position.z), (int)camera_position.z);
				m_grass_matrices[i].setTranslation(pos);
			}
		}*/
	}


	void Terrain::grassLoaded(Resource::State, Resource::State)
	{
		if (m_grass_model->isReady())
		{
			LUMIX_DELETE(m_grass_geometry);

			m_grass_geometry = LUMIX_NEW(Geometry);
			m_grass_geometry->copy(*m_grass_model->getGeometry(), COPY_COUNT);
			Material* material = m_grass_model->getMesh(0).getMaterial();
			m_grass_mesh = LUMIX_NEW(Mesh)(material, 0, m_grass_model->getMesh(0).getCount() * COPY_COUNT, "grass");
		}
	}


	void Terrain::getGrassInfos(Array<GrassInfo>& infos, const Vec3& camera_position)
	{
		if (m_grass_geometry && m_grass_model->isReady())
		{
			updateGrass(camera_position);
			for (int i = 0; i < GRASS_QUADS_WIDTH; ++i)
			{
				for (int j = 0; j < GRASS_QUADS_HEIGHT; ++j)
				{
					{
						GrassInfo& info = infos.pushEmpty();
						info.m_geometry = m_grass_geometry;
						info.m_matrices = &m_grass_quads[i][j].m_matrices[0];
						info.m_mesh = m_grass_mesh;
					}
					{
						GrassInfo& info = infos.pushEmpty();
						info.m_geometry = m_grass_geometry;
						info.m_matrices = &m_grass_quads[i][j].m_matrices[50];
						info.m_mesh = m_grass_mesh;
					}
				}
			}
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
			if (m_mesh && m_material)
			{
				m_mesh->setMaterial(m_material);
				m_material->getObserverCb().bind<Terrain, &Terrain::onMaterialLoaded>(this);
				if (m_material->isReady())
				{
					onMaterialLoaded(Resource::State::READY, Resource::State::READY);
				}
			}
		}
		else if(material)
		{
			material->getResourceManager().get(ResourceManager::MATERIAL)->unload(*material);
		}

		setGrass("models/grass/grass.msh");
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
			Matrix world_matrix;
			m_entity.getMatrix(world_matrix);
			world_matrix.fastInverse();
			Vec3 rel_cam_pos = world_matrix.multiplyPosition(camera_pos) / m_xz_scale;
			m_mesh->getMaterial()->getShader()->setUniform("map_size", m_root->m_size);
			m_mesh->getMaterial()->getShader()->setUniform("camera_pos", rel_cam_pos);
			m_root->render(m_mesh, m_geometry, rel_cam_pos, *pipeline.getScene());
		}
	}


	float Terrain::getHeight(int x, int z)
	{
		Texture* t = m_material->getTexture(0);
		int idx = x + z * m_width;
		if (t->getBytesPerPixel() == 2)
		{
			return ((m_y_scale / (256.0f * 256.0f - 1)) * ((uint16_t*)t->getData())[idx]);
		}
		else if(t->getBytesPerPixel() == 4)
		{
			return ((m_y_scale / 255.0f) * ((uint8_t*)t->getData())[idx * 4]);
		}
		else
		{
			ASSERT(false); TODO("todo");
		}
		return 0;
	}

	bool getRayTriangleIntersection(const Vec3& local_origin, const Vec3& local_dir, const Vec3& p0, const Vec3& p1, const Vec3& p2, float& out)
	{
		Vec3 normal = crossProduct(p1 - p0, p2 - p0);
		float q = dotProduct(normal, local_dir);
		if (q == 0)
		{
			return false;
		}
		float d = -dotProduct(normal, p0);
		float t = -(dotProduct(normal, local_origin) + d) / q;
		if (t < 0)
		{
			return false;
		}
		Vec3 hit_point = local_origin + local_dir * t;

		Vec3 edge0 = p1 - p0;
		Vec3 VP0 = hit_point - p0;
		if (dotProduct(normal, crossProduct(edge0, VP0)) < 0)
		{
			return false;
		}

		Vec3 edge1 = p2 - p1;
		Vec3 VP1 = hit_point - p1;
		if (dotProduct(normal, crossProduct(edge1, VP1)) < 0)
		{
			return false;
		}

		Vec3 edge2 = p0 - p2;
		Vec3 VP2 = hit_point - p2;
		if (dotProduct(normal, crossProduct(edge2, VP2)) < 0)
		{
			return false;
		}

		out = t;
		return true;
	}

	
	RayCastModelHit Terrain::castRay(const Vec3& origin, const Vec3& dir)
	{
		RayCastModelHit hit;
		hit.m_is_hit = false;
		if (m_material && m_material->isReady())
		{
			Matrix mtx = m_entity.getMatrix();
			mtx.fastInverse();
			Vec3 rel_origin = mtx.multiplyPosition(origin);
			Vec3 rel_dir = mtx * dir;
			Vec3 start;
			Vec3 size(m_root->m_size * m_xz_scale, m_y_scale, m_root->m_size * m_xz_scale);
			if (Math::getRayAABBIntersection(rel_origin, rel_dir, m_root->m_min, size, start))
			{
				Vec3 p = start;
				int hx = (int)(p.x / m_xz_scale);
				int hz = (int)(p.z / m_xz_scale);
				while (hx >= 0 && hz >= 0 && hx < m_width - 1 && hz < m_height - 1 && p.y > m_root->m_min.y && p.y < m_root->m_min.y + m_root->m_size)
				{
					float t;
					float x = hx * m_xz_scale;
					float z = hz * m_xz_scale;
					Vec3 p0(x, getHeight(hx, hz), z);
					Vec3 p1(x + m_xz_scale, getHeight(hx + 1, hz), z);
					Vec3 p2(x + m_xz_scale, getHeight(hx + 1, hz + 1), z + m_xz_scale);
					Vec3 p3(x, getHeight(hx, hz + 1), z + m_xz_scale);
					if (getRayTriangleIntersection(rel_origin, rel_dir, p0, p1, p2, t))
					{
						hit.m_is_hit = true;
						hit.m_origin = origin;
						hit.m_dir = dir;
						hit.m_t = t;
						return hit;
					}
					if (getRayTriangleIntersection(rel_origin, rel_dir, p0, p2, p3, t))
					{
						hit.m_is_hit = true;
						hit.m_origin = origin;
						hit.m_dir = dir;
						hit.m_t = t;
						return hit;
					}
					p += rel_dir;
					hx = (int)(p.x / m_xz_scale);
					hz = (int)(p.z / m_xz_scale);
				}
			}
		}
		return hit;
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