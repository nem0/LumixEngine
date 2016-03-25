#include "terrain.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/geometry.h"
#include "core/json_serializer.h"
#include "core/lifo_allocator.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "engine.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "universe/universe.h"
#include <cfloat>
#include <cmath>


namespace Lumix
{


static const int GRASS_QUAD_SIZE = 10;
static const float GRASS_QUAD_RADIUS = GRASS_QUAD_SIZE * 0.7072f;
static const int GRID_SIZE = 16;
static const int COPY_COUNT = 50;
static const uint32 TERRAIN_HASH = crc32("terrain");
static const uint32 MORPH_CONST_HASH = crc32("morph_const");
static const uint32 QUAD_SIZE_HASH = crc32("quad_size");
static const uint32 QUAD_MIN_HASH = crc32("quad_min");

static const uint32 BRUSH_POSITION_HASH = crc32("brush_position");
static const uint32 BRUSH_SIZE_HASH = crc32("brush_size");
static const uint32 MAP_SIZE_HASH = crc32("map_size");
static const uint32 CAMERA_POS_HASH = crc32("camera_pos");
static const char* TEX_COLOR_UNIFORM = "u_texColor";

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

	explicit TerrainQuad(IAllocator& allocator)
		: m_allocator(allocator)
	{
		for (int i = 0; i < CHILD_COUNT; ++i)
		{
			m_children[i] = nullptr;
		}
	}

	~TerrainQuad()
	{
		for (int i = 0; i < CHILD_COUNT; ++i)
		{
			LUMIX_DELETE(m_allocator, m_children[i]);
		}
	}

	void createChildren()
	{
		if (m_lod < 16 && m_size > 16)
		{
			for (int i = 0; i < CHILD_COUNT; ++i)
			{
				m_children[i] = LUMIX_NEW(m_allocator, TerrainQuad)(m_allocator);
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

	float getSquaredDistance(const Vec3& camera_pos)
	{
		Vec3 _min = m_min;
		Vec3 _max(_min.x + m_size, _min.y, _min.z + m_size);
		float dist = 0;
		if (camera_pos.x < _min.x)
		{
			float d = _min.x - camera_pos.x;
			dist += d*d;
		}
		if (camera_pos.x > _max.x)
		{
			float d = _max.x - camera_pos.x;
			dist += d*d;
		}
		if (camera_pos.z < _min.z)
		{
			float d = _min.z - camera_pos.z;
			dist += d*d;
		}
		if (camera_pos.z > _max.z)
		{
			float d = _max.z - camera_pos.z;
			dist += d*d;
		}
		return dist;
	}

	static float getRadiusInner(float size)
	{
		float lower_level_size = size * 0.5f;
		float lower_level_diagonal = Math::SQRT2 * size * 0.5f;
		return getRadiusOuter(lower_level_size) + lower_level_diagonal;
	}

	static float getRadiusOuter(float size)
	{
		return (size > 17 ? 2.25f : 1.25f) * Math::SQRT2 * size;
	}

	bool getInfos(Array<const TerrainInfo*>& infos,
		const Vec3& camera_pos,
		Terrain* terrain,
		const Matrix& world_matrix,
		LIFOAllocator& allocator)
	{
		float squared_dist = getSquaredDistance(camera_pos);
		float r = getRadiusOuter(m_size);
		if (squared_dist > r * r && m_lod > 1) return false;

		Vec3 morph_const(r, getRadiusInner(m_size), 0);
		Shader& shader = *terrain->getMesh()->material->getShader();
		for (int i = 0; i < CHILD_COUNT; ++i)
		{
			if (!m_children[i] ||
				!m_children[i]->getInfos(infos, camera_pos, terrain, world_matrix, allocator))
			{
				TerrainInfo* data = (TerrainInfo*)allocator.allocate(sizeof(TerrainInfo));
				data->m_morph_const = morph_const;
				data->m_index = i;
				data->m_terrain = terrain;
				data->m_size = m_size;
				data->m_min = m_min;
				data->m_shader = &shader;
				data->m_world_matrix = world_matrix;
				infos.push(data);
			}
		}
		return true;
	}


	IAllocator& m_allocator;
	TerrainQuad* m_children[CHILD_COUNT];
	Vec3 m_min;
	float m_size;
	int m_lod;
};


Terrain::Terrain(Renderer& renderer, Entity entity, RenderScene& scene, IAllocator& allocator)
	: m_mesh(nullptr)
	, m_material(nullptr)
	, m_root(nullptr)
	, m_detail_texture(nullptr)
	, m_heightmap(nullptr)
	, m_splatmap(nullptr)
	, m_width(0)
	, m_height(0)
	, m_layer_mask(1)
	, m_scale(1, 1, 1)
	, m_entity(entity)
	, m_scene(scene)
	, m_allocator(allocator)
	, m_grass_quads(m_allocator)
	, m_last_camera_position(m_allocator)
	, m_grass_types(m_allocator)
	, m_free_grass_quads(m_allocator)
	, m_renderer(renderer)
	, m_vertices_handle(BGFX_INVALID_HANDLE)
	, m_indices_handle(BGFX_INVALID_HANDLE)
	, m_grass_distance(5)
{
	generateGeometry();
}

Terrain::GrassType::~GrassType()
{
	if (m_grass_model)
	{
		m_grass_model->getResourceManager().get(ResourceManager::MODEL)->unload(*m_grass_model);
		m_grass_model->getObserverCb().unbind<GrassType, &GrassType::grassLoaded>(this);
	}
}

Terrain::~Terrain()
{
	bgfx::destroyIndexBuffer(m_indices_handle);
	bgfx::destroyVertexBuffer(m_vertices_handle);

	setMaterial(nullptr);
	LUMIX_DELETE(m_allocator, m_mesh);
	LUMIX_DELETE(m_allocator, m_root);
	for(int i = 0; i < m_grass_types.size(); ++i)
	{
		LUMIX_DELETE(m_allocator, m_grass_types[i]);
	}
	for (int j = 0; j < m_grass_quads.size(); ++j)
	{
		Array<GrassQuad*>& quads = m_grass_quads.at(j);
		for (int i = 0; i < quads.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, quads[i]);
		}
	}
	for (int i = 0; i < m_free_grass_quads.size(); ++i)
	{
		LUMIX_DELETE(m_allocator, m_free_grass_quads[i]);
	}
}


Terrain::GrassType::GrassType(Terrain& terrain)
	: m_terrain(terrain)
{
	m_grass_model = nullptr;
	m_ground = 0;
	m_density = 10;
	m_distance = 50;
}


float Terrain::getRootSize() const 
{
	return m_root ? m_root->m_size : 0;
}


void Terrain::addGrassType(int index)
{
	if(index < 0)
	{
		m_grass_types.push(LUMIX_NEW(m_allocator, GrassType)(*this));
	}
	else
	{
		m_grass_types.insert(index, LUMIX_NEW(m_allocator, GrassType)(*this));
	}
}


void Terrain::removeGrassType(int index)
{
	forceGrassUpdate();
	LUMIX_DELETE(m_allocator, m_grass_types[index]);
	m_grass_types.erase(index);
}


void Terrain::setGrassTypeDensity(int index, int density)
{
	forceGrassUpdate();
	GrassType& type = *m_grass_types[index];
	type.m_density = Math::clamp(density, 0, 50);
}


int Terrain::getGrassTypeDensity(int index) const
{
	GrassType& type = *m_grass_types[index];
	return type.m_density;
}


void Terrain::setGrassTypeDistance(int index, float distance)
{
	forceGrassUpdate();
	m_grass_distance = 0;
	for (auto* type : m_grass_types)
	{
		m_grass_distance = Math::maximum(m_grass_distance, int(distance / GRASS_QUAD_RADIUS + 0.99f));
	}
	GrassType& type = *m_grass_types[index];
	type.m_distance = Math::clamp(distance, 1.0f, FLT_MAX);
}


float Terrain::getGrassTypeDistance(int index) const
{
	GrassType& type = *m_grass_types[index];
	return type.m_distance;
}


void Terrain::setGrassTypeGround(int index, int ground)
{
	ground = Math::clamp(ground, 0, 3);
	forceGrassUpdate();
	GrassType& type = *m_grass_types[index];
	type.m_ground = ground;
}
	
	
int Terrain::getGrassTypeGround(int index) const
{
	GrassType& type = *m_grass_types[index];
	return type.m_ground;
}


AABB Terrain::getAABB() const
{
	Vec3 min(0, 0, 0);
	Vec3 max(m_width * m_scale.x, 0, m_height * m_scale.z);
	for (int j = 0; j < m_height; ++j)
	{
		for (int i = 0; i < m_width; ++i)
		{
			float height = getHeight(i, j);
			if (height > max.y) max.y = height;
		}
	}
			
	return AABB(min, max);
}


Path Terrain::getGrassTypePath(int index)
{
	GrassType& type = *m_grass_types[index];
	if (type.m_grass_model)
	{
		return type.m_grass_model->getPath();
	}
	return Path("");
}


void Terrain::setGrassTypePath(int index, const Path& path)
{
	forceGrassUpdate();
	GrassType& type = *m_grass_types[index];
	if (type.m_grass_model)
	{
		type.m_grass_model->getResourceManager().get(ResourceManager::MODEL)->unload(*type.m_grass_model);
		type.m_grass_model->getObserverCb().unbind<GrassType, &GrassType::grassLoaded>(&type);
		type.m_grass_model = nullptr;
	}
	if (path.isValid())
	{
		type.m_grass_model = static_cast<Model*>(m_scene.getEngine().getResourceManager().get(ResourceManager::MODEL)->load(path));
		type.m_grass_model->onLoaded<GrassType, &GrassType::grassLoaded>(&type);
	}
}
	

void Terrain::forceGrassUpdate()
{
	m_force_grass_update = true;
	for (int i = 0; i < m_grass_quads.size(); ++i)
	{
		Array<GrassQuad*>& quads = m_grass_quads.at(i);
		while(!quads.empty())
		{
			m_free_grass_quads.push(quads.back());
			quads.pop();
		}
	}
}

Array<Terrain::GrassQuad*>& Terrain::getQuads(ComponentIndex camera)
{
	int quads_index = m_grass_quads.find(camera);
	if (quads_index < 0)
	{
		m_grass_quads.insert(camera, Array<GrassQuad*>(m_allocator));
		quads_index = m_grass_quads.find(camera);
	}
	return m_grass_quads.at(quads_index);
}


void Terrain::generateGrassTypeQuad(GrassPatch& patch,
									const Matrix& terrain_matrix,
									float quad_x,
									float quad_z)
{
	if (!patch.m_type->m_grass_model || !patch.m_type->m_grass_model->isReady())
		return;

	Texture* splat_map = m_splatmap;
	float step = GRASS_QUAD_SIZE / (float)patch.m_type->m_density;

	for (float dx = 0; dx < GRASS_QUAD_SIZE; dx += step)
	{
		for (float dz = 0; dz < GRASS_QUAD_SIZE; dz += step)
		{
			uint32 pixel_value = splat_map->getPixelNearest(
				int(splat_map->getWidth() * (quad_x + dx) /
					(m_width * m_scale.x)),
				int(splat_map->getHeight() * (quad_z + dz) /
					(m_height * m_scale.x)));

			int ground_index = pixel_value & 0xff;
			int weight = (pixel_value >> 8) & 0xff;
			uint8 count = ground_index == patch.m_type->m_ground ? weight : 0;
			float density = count / 255.0f;

			if (density < 0.25f) continue;

			Matrix& grass_mtx = patch.m_matrices.emplace();
			grass_mtx = Matrix::IDENTITY;
			float x = quad_x + dx + step * Math::randFloat(-0.5f, 0.5f);
			float z = quad_z + dz + step * Math::randFloat(-0.5f, 0.5f);
			grass_mtx.setTranslation(Vec3(x, getHeight(x, z), z));
			Quat q(Vec3(0, 1, 0), Math::randFloat(0, Math::PI * 2));
			Matrix rotMatrix;
			q.toMatrix(rotMatrix);
			grass_mtx = terrain_matrix * grass_mtx * rotMatrix;
			grass_mtx.multiply3x3(density + Math::randFloat(-0.1f, 0.1f));
		}
	}
}


void Terrain::updateGrass(ComponentIndex camera)
{
	PROFILE_FUNCTION();
	if (!m_splatmap)
		return;

	Array<GrassQuad*>& quads = getQuads(camera);

	if (m_free_grass_quads.size() + quads.size() < m_grass_distance * m_grass_distance)
	{
		int new_count = m_grass_distance * m_grass_distance - quads.size();
		for (int i = 0; i < new_count; ++i)
		{
			m_free_grass_quads.push(LUMIX_NEW(m_allocator, GrassQuad)(m_allocator));
		}
	}

	Universe& universe = m_scene.getUniverse();
	Entity camera_entity = m_scene.getCameraEntity(camera);
	Vec3 camera_pos = universe.getPosition(camera_entity);

	if ((m_last_camera_position[camera] - camera_pos).length() <= FLT_MIN && !m_force_grass_update)
		return;
	m_last_camera_position[camera] = camera_pos;

	m_force_grass_update = false;
	Matrix mtx = universe.getMatrix(m_entity);
	Matrix inv_mtx = mtx;
	inv_mtx.fastInverse();
	Vec3 local_camera_pos = inv_mtx.multiplyPosition(camera_pos);
	float cx = (int)(local_camera_pos.x / (GRASS_QUAD_SIZE)) * (float)GRASS_QUAD_SIZE;
	float cz = (int)(local_camera_pos.z / (GRASS_QUAD_SIZE)) * (float)GRASS_QUAD_SIZE;
	float from_quad_x = cx - (m_grass_distance >> 1) * GRASS_QUAD_SIZE;
	float from_quad_z = cz - (m_grass_distance >> 1) * GRASS_QUAD_SIZE;
	float to_quad_x = cx + (m_grass_distance >> 1) * GRASS_QUAD_SIZE;
	float to_quad_z = cz + (m_grass_distance >> 1) * GRASS_QUAD_SIZE;

	float old_bounds[4] = {FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX};
	for (int i = quads.size() - 1; i >= 0; --i)
	{
		GrassQuad* quad = quads[i];
		old_bounds[0] = Math::minimum(old_bounds[0], quad->pos.x);
		old_bounds[1] = Math::maximum(old_bounds[1], quad->pos.x);
		old_bounds[2] = Math::minimum(old_bounds[2], quad->pos.z);
		old_bounds[3] = Math::maximum(old_bounds[3], quad->pos.z);
		if (quad->pos.x < from_quad_x || quad->pos.x > to_quad_x || quad->pos.z < from_quad_z ||
			quad->pos.z > to_quad_z)
		{
			m_free_grass_quads.push(quads[i]);
			quads.eraseFast(i);
		}
	}

	from_quad_x = Math::maximum(0.0f, from_quad_x);
	from_quad_z = Math::maximum(0.0f, from_quad_z);

	for (float quad_z = from_quad_z; quad_z <= to_quad_z; quad_z += GRASS_QUAD_SIZE)
	{
		for (float quad_x = from_quad_x; quad_x <= to_quad_x; quad_x += GRASS_QUAD_SIZE)
		{
			if (quad_x >= old_bounds[0] && quad_x <= old_bounds[1] && quad_z >= old_bounds[2] &&
				quad_z <= old_bounds[3])
				continue;

			GrassQuad* quad = nullptr;
			if (!m_free_grass_quads.empty())
			{
				quad = m_free_grass_quads.back();
				m_free_grass_quads.pop();
			}
			else
			{
				quad = LUMIX_NEW(m_allocator, GrassQuad)(m_allocator);
			}
			quads.push(quad);
			quad->pos.x = quad_x;
			quad->pos.z = quad_z;
			quad->m_patches.clear();
			srand((int)quad_x + (int)quad_z * m_grass_distance);

			float min_y = FLT_MAX;
			float max_y = -FLT_MAX;
			for (auto* grass_type : m_grass_types)
			{
				Model* model = grass_type->m_grass_model;
				if (!model || !model->isReady()) continue;
				GrassPatch& patch = quad->m_patches.emplace(m_allocator);
				patch.m_matrices.clear();
				patch.m_type = grass_type;

				generateGrassTypeQuad(patch, mtx, quad_x, quad_z);
				for (auto mtx : patch.m_matrices)
				{
					min_y = Math::minimum(mtx.getTranslation().y, min_y);
					max_y = Math::maximum(mtx.getTranslation().y, max_y);
				}
			}

			quad->pos.y = (max_y + min_y) * 0.5f;
			quad->radius = Math::maximum((max_y - min_y) * 0.5f, (float)GRASS_QUAD_SIZE) * 1.42f;

		}
	}
}


void Terrain::GrassType::grassLoaded(Resource::State, Resource::State)
{
	m_terrain.forceGrassUpdate();
}


void Terrain::getGrassInfos(const Frustum& frustum, Array<GrassInfo>& infos, ComponentIndex camera)
{
	if (!m_material || !m_material->isReady()) return;

	updateGrass(camera);
	Array<GrassQuad*>& quads = getQuads(camera);
	
	Universe& universe = m_scene.getUniverse();
	Matrix mtx = universe.getMatrix(m_entity);
	Vec3 frustum_position = frustum.position;
	for (auto* quad : quads)
	{
		Vec3 quad_center(quad->pos.x + GRASS_QUAD_SIZE * 0.5f, quad->pos.y, quad->pos.z + GRASS_QUAD_SIZE * 0.5f);
		quad_center = mtx.multiplyPosition(quad_center);
		if (frustum.isSphereInside(quad_center, quad->radius))
		{
			float dist2 = (quad_center - frustum_position).squaredLength();
			for (int patch_idx = 0; patch_idx < quad->m_patches.size(); ++patch_idx)
			{
				const GrassPatch& patch = quad->m_patches[patch_idx];
				if (patch.m_type->m_distance * patch.m_type->m_distance < dist2) continue;
				if (!patch.m_matrices.empty())
				{
					GrassInfo& info = infos.emplace();
					info.matrices = &patch.m_matrices[0];
					info.matrix_count = patch.m_matrices.size();
					info.model = patch.m_type->m_grass_model;
					info.type_distance = patch.m_type->m_distance;
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
		m_splatmap = nullptr;
		m_heightmap = nullptr;
		if (m_mesh && m_material)
		{
			m_mesh->material = m_material;
			m_material->onLoaded<Terrain, &Terrain::onMaterialLoaded>(this);
		}
	}
	else if(material)
	{
		material->getResourceManager().get(ResourceManager::MATERIAL)->unload(*material);
	}
}

void Terrain::deserialize(InputBlob& serializer, Universe& universe, RenderScene& scene, int index, int version)
{
	serializer.read(m_entity);
	serializer.read(m_layer_mask);
	char path[MAX_PATH_LENGTH];
	serializer.readString(path, MAX_PATH_LENGTH);
	setMaterial(static_cast<Material*>(scene.getEngine().getResourceManager().get(ResourceManager::MATERIAL)->load(Path(path))));
	serializer.read(m_scale.x);
	serializer.read(m_scale.y);
	m_scale.z = m_scale.x;
	int32 count;
	serializer.read(m_grass_distance);
	serializer.read(count);
	while(m_grass_types.size() > count)
	{
		removeGrassType(m_grass_types.size() - 1);
	}

	while(m_grass_types.size() < count)
	{
		addGrassType(-1);
	}
	for(int i = 0; i < count; ++i)
	{
		serializer.readString(path, MAX_PATH_LENGTH);
		serializer.read(m_grass_types[i]->m_ground);
		serializer.read(m_grass_types[i]->m_density);
		if (version > (int)RenderSceneVersion::GRASS_TYPE_DISTANCE)
		{
			serializer.read(m_grass_types[i]->m_distance);
		}
		setGrassTypePath(i, Path(path));
	}
	universe.addComponent(m_entity, TERRAIN_HASH, &scene, index);
}

	
void Terrain::serialize(OutputBlob& serializer)
{
	serializer.write(m_entity);
	serializer.write(m_layer_mask);
	serializer.writeString(m_material ? m_material->getPath().c_str() : "");
	serializer.write(m_scale.x);
	serializer.write(m_scale.y);
	serializer.write(m_grass_distance);
	serializer.write((int32)m_grass_types.size());
	for(int i = 0; i < m_grass_types.size(); ++i)
	{
		GrassType& type = *m_grass_types[i];
		serializer.writeString(type.m_grass_model ? type.m_grass_model->getPath().c_str() : "");
		serializer.write(type.m_ground);
		serializer.write(type.m_density);
		serializer.write(type.m_distance);
	}
}


void Terrain::getInfos(Array<const TerrainInfo*>& infos, const Vec3& camera_pos, LIFOAllocator& allocator)
{
	if (!m_root) return;
	if (!m_material || !m_material->isReady()) return;

	Matrix matrix = m_scene.getUniverse().getMatrix(m_entity);
	Matrix inv_matrix = matrix;
	inv_matrix.fastInverse();
	Vec3 local_camera_pos = inv_matrix.multiplyPosition(camera_pos);
	local_camera_pos.x /= m_scale.x;
	local_camera_pos.z /= m_scale.z;
	m_root->getInfos(infos, local_camera_pos, this, matrix, allocator);
}


Vec3 Terrain::getNormal(float x, float z)
{
	int int_x = (int)(x / m_scale.x);
	int int_z = (int)(z / m_scale.x);
	float dec_x = (x - (int_x * m_scale.x)) / m_scale.x;
	float dec_z = (z - (int_z * m_scale.x)) / m_scale.x;
	if (dec_x > dec_z)
	{
		float h0 = getHeight(int_x, int_z);
		float h1 = getHeight(int_x + 1, int_z);
		float h2 = getHeight(int_x + 1, int_z + 1);
		return crossProduct(Vec3(m_scale.x, h2 - h0, m_scale.x), Vec3(m_scale.x, h1 - h0, 0)).normalized();
	}
	else
	{
		float h0 = getHeight(int_x, int_z);
		float h1 = getHeight(int_x + 1, int_z + 1);
		float h2 = getHeight(int_x, int_z + 1);
		return crossProduct(Vec3(0, h2 - h0, m_scale.x), Vec3(m_scale.x, h1 - h0, m_scale.x)).normalized();
	}
}

	
float Terrain::getHeight(float x, float z) const
{
	int int_x = (int)(x / m_scale.x);
	int int_z = (int)(z / m_scale.x);
	float dec_x = (x - (int_x * m_scale.x)) / m_scale.x;
	float dec_z = (z - (int_z * m_scale.x)) / m_scale.x;
	if (dec_z == 0 && dec_x == 0)
	{
		return getHeight(int_x, int_z);
	}
	else if (dec_x > dec_z)
	{
		float h0 = getHeight(int_x, int_z);
		float h1 = getHeight(int_x + 1, int_z);
		float h2 = getHeight(int_x + 1, int_z + 1);
		return h0 + (h1 - h0) * dec_x + (h2 - h1) * dec_z;
	}
	else
	{
		float h0 = getHeight(int_x, int_z);
		float h1 = getHeight(int_x + 1, int_z + 1);
		float h2 = getHeight(int_x, int_z + 1);
		return h0 + (h2 - h0) * dec_z + (h1 - h2) * dec_x;
	}
}
	

float Terrain::getHeight(int x, int z) const
{
	if (!m_heightmap) return 0;

	int texture_x = x;
	int texture_y = z;
	Texture* t = m_heightmap;
	int idx = Math::clamp(texture_x, 0, m_width) + Math::clamp(texture_y, 0, m_height) * m_width;
	if (t->getBytesPerPixel() == 2)
	{
		return m_scale.y / 65535.0f * ((uint16*)t->getData())[idx];
	}
	else if(t->getBytesPerPixel() == 4)
	{
		return ((m_scale.y / 255.0f) * ((uint8*)t->getData())[idx * 4]);
	}
	else
	{
		ASSERT(false);
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
	if (m_root)
	{
		Matrix mtx = m_scene.getUniverse().getMatrix(m_entity);
		mtx.fastInverse();
		Vec3 rel_origin = mtx.multiplyPosition(origin);
		Vec3 rel_dir = mtx * Vec4(dir, 0);
		Vec3 start;
		Vec3 size(m_root->m_size * m_scale.x, m_scale.y * 65535.0f, m_root->m_size * m_scale.x);
		if (Math::getRayAABBIntersection(rel_origin, rel_dir, m_root->m_min, size, start))
		{
			int hx = (int)(start.x / m_scale.x);
			int hz = (int)(start.z / m_scale.x);

			float next_x = fabs(rel_dir.x) < 0.01f ? hx : ((hx + (rel_dir.x < 0 ? 0 : 1)) * m_scale.x - rel_origin.x) / rel_dir.x;
			float next_z = fabs(rel_dir.z) < 0.01f ? hz : ((hz + (rel_dir.z < 0 ? 0 : 1)) * m_scale.x - rel_origin.z) / rel_dir.z;

			float delta_x = fabs(rel_dir.x) < 0.01f ? 0 : m_scale.x / Math::abs(rel_dir.x);
			float delta_z = fabs(rel_dir.z) < 0.01f ? 0 : m_scale.x / Math::abs(rel_dir.z);
			int step_x = (int)Math::signum(rel_dir.x);
			int step_z = (int)Math::signum(rel_dir.z);

			while (hx >= 0 && hz >= 0 && hx + step_x < m_width && hz + step_z < m_height)
			{
				float t;
				float x = hx * m_scale.x;
				float z = hz * m_scale.x;
				Vec3 p0(x, getHeight(x, z), z);
				Vec3 p1(x + m_scale.x, getHeight(x + m_scale.x, z), z);
				Vec3 p2(x + m_scale.x, getHeight(x + m_scale.x, z + m_scale.x), z + m_scale.x);
				Vec3 p3(x, getHeight(x, z + m_scale.x), z + m_scale.x);
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
				if (next_x < next_z)
				{
					next_x += delta_x;
					hx += step_x;
				}
				else
				{
					next_z += delta_z;
					hz += step_z;
				}
				if (delta_x == 0 && delta_z == 0)
				{
					return hit;
				}
			}
		}
	}
	return hit;
}


static void generateSubgrid(Array<Sample>& samples, Array<short>& indices, int& indices_offset, int start_x, int start_y)
{
	for (int j = start_y; j < start_y + 8; ++j)
	{
		for (int i = start_x; i < start_x + 8; ++i)
		{
			short idx = short(4 * (i + j * GRID_SIZE));
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
	LUMIX_DELETE(m_allocator, m_mesh);
	m_mesh = nullptr;
	Array<Sample> points(m_allocator);
	points.resize(GRID_SIZE * GRID_SIZE * 4);
	Array<short> indices(m_allocator);
	indices.resize(GRID_SIZE * GRID_SIZE * 6);
	int indices_offset = 0;
	generateSubgrid(points, indices, indices_offset, 0, 0);
	generateSubgrid(points, indices, indices_offset, 8, 0);
	generateSubgrid(points, indices, indices_offset, 0, 8);
	generateSubgrid(points, indices, indices_offset, 8, 8);

	bgfx::VertexDecl vertex_def;
	vertex_def.begin()
		.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.end();
	m_vertices_handle = bgfx::createVertexBuffer(bgfx::copy(&points[0], sizeof(points[0]) * points.size()), vertex_def);
	auto* indices_mem = bgfx::copy(&indices[0], sizeof(indices[0]) * indices.size());
	m_indices_handle = bgfx::createIndexBuffer(indices_mem);
	m_mesh = LUMIX_NEW(m_allocator, Mesh)(vertex_def,
		m_material,
		0,
		int(points.size() * sizeof(points[0])),
		0,
		int(indices.size()),
		"terrain",
		m_allocator);
}

TerrainQuad* Terrain::generateQuadTree(float size)
{
	TerrainQuad* root = LUMIX_NEW(m_allocator, TerrainQuad)(m_allocator);
	root->m_lod = 1;
	root->m_min.set(0, 0, 0);
	root->m_size = size;
	root->createChildren();
	return root;
}

void Terrain::onMaterialLoaded(Resource::State, Resource::State new_state)
{
	PROFILE_FUNCTION();
	if (new_state == Resource::State::READY)
	{
		m_detail_texture = m_material->getTextureByUniform(TEX_COLOR_UNIFORM);

		m_heightmap = m_material->getTextureByUniform("u_texHeightmap");
		bool is_data_ready = true;
		if (m_heightmap && m_heightmap->getData() == nullptr)
		{
			m_heightmap->addDataReference();
			is_data_ready = false;
		}
		m_splatmap = m_material->getTextureByUniform("u_texSplatmap");
		if (m_splatmap && m_splatmap->getData() == nullptr)
		{
			m_splatmap->addDataReference();
			is_data_ready = false;
		}

		Texture* colormap = m_material->getTextureByUniform("u_texColormap");
		if (colormap && colormap->getData() == nullptr)
		{
			colormap->addDataReference();
			is_data_ready = false;
		}

		if (is_data_ready)
		{
			LUMIX_DELETE(m_allocator, m_root);
			if (m_heightmap && m_splatmap)
			{
				m_width = m_heightmap->getWidth();
				m_height = m_heightmap->getHeight();
				m_root = generateQuadTree((float)m_width);
			}
		}
	}
	else
	{
		LUMIX_DELETE(m_allocator, m_root);
		m_root = nullptr;
	}
}


} // namespace Lumix