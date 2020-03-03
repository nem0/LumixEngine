#include "terrain.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "engine/universe.h"


namespace Lumix
{


static const float GRASS_QUAD_SIZE = 10.0f;
static const float GRASS_QUAD_RADIUS = GRASS_QUAD_SIZE * 0.7072f;
static const ComponentType TERRAIN_HASH = Reflection::getComponentType("terrain");

struct Sample
{
	Vec3 pos;
	float u, v;
};


Terrain::Terrain(Renderer& renderer, EntityPtr entity, RenderScene& scene, IAllocator& allocator)
	: m_material(nullptr)
	, m_albedomap(nullptr)
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
	, m_renderer(renderer)
	, m_force_grass_update(false)
{
}

Terrain::GrassType::~GrassType()
{
	if (m_grass_model)
	{
		m_grass_model->getObserverCb().unbind<&Terrain::grassLoaded>(&m_terrain);
		m_grass_model->getResourceManager().unload(*m_grass_model);
	}
}

Terrain::~Terrain()
{
	setMaterial(nullptr);
	for (const Array<GrassQuad*>& quads : m_grass_quads) {
		for (GrassQuad* quad : quads) {
			LUMIX_DELETE(m_allocator, quad);
		}
	}
}


Terrain::GrassType::GrassType(Terrain& terrain)
	: m_terrain(terrain)
{
	m_grass_model = nullptr;
	m_density = 10;
	m_distance = 50;
}


void Terrain::addGrassType(int index)
{
	forceGrassUpdate();
	if(index < 0)
	{
		int idx = m_grass_types.size();
		m_grass_types.emplace(*this).m_idx = idx;
	}
	else
	{
		GrassType type(*this);
		type.m_idx = index;
		m_grass_types.insert(index, type);
	}
}


void Terrain::removeGrassType(int index)
{
	forceGrassUpdate();
	m_grass_types.erase(index);
}


void Terrain::setGrassTypeDensity(int index, int density)
{
	forceGrassUpdate();
	GrassType& type = m_grass_types[index];
	type.m_density = clamp(density, 0, 50);
}


Terrain::GrassType::RotationMode Terrain::getGrassTypeRotationMode(int index) const
{
	const GrassType& type = m_grass_types[index];
	return type.m_rotation_mode;
}


void Terrain::setGrassTypeRotationMode(int index, Terrain::GrassType::RotationMode mode)
{
	m_grass_types[index].m_rotation_mode = mode;
	forceGrassUpdate();
}


int Terrain::getGrassTypeDensity(int index) const
{
	const GrassType& type = m_grass_types[index];
	return type.m_density;
}


void Terrain::setGrassTypeDistance(int index, float distance)
{
	forceGrassUpdate();
	GrassType& type = m_grass_types[index];
	type.m_distance = clamp(distance, 1.0f, FLT_MAX);
}


float Terrain::getGrassTypeDistance(int index) const
{
	const GrassType& type = m_grass_types[index];
	return type.m_distance;
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
	GrassType& type = m_grass_types[index];
	if (type.m_grass_model)
	{
		return type.m_grass_model->getPath();
	}
	return Path("");
}


void Terrain::setGrassTypePath(int index, const Path& path)
{
	forceGrassUpdate();
	GrassType& type = m_grass_types[index];
	if (type.m_grass_model)
	{
		type.m_grass_model->getResourceManager().unload(*type.m_grass_model);
		type.m_grass_model->getObserverCb().unbind<&Terrain::grassLoaded>(this);
		type.m_grass_model = nullptr;
	}
	if (path.isValid())
	{
		type.m_grass_model = m_scene.getEngine().getResourceManager().load<Model>(path);
		type.m_grass_model->onLoaded<&Terrain::grassLoaded>(this);
	}
}
	

void Terrain::forceGrassUpdate()
{
	m_force_grass_update = true;
	for (Array<GrassQuad*>& quads : m_grass_quads) {
		for (GrassQuad* quad : quads) {
			LUMIX_DELETE(m_allocator, quad);
		}
		quads.clear();
	}
}

Array<Terrain::GrassQuad*>& Terrain::getQuads(int view)
{
	while (view >= m_grass_quads.size()) m_grass_quads.emplace(m_allocator);
	return m_grass_quads[view];
}


void Terrain::generateGrassTypeQuad(GrassPatch& patch, const RigidTransform& terrain_tr, const Vec2& quad_pos)
{
	if (m_splatmap->data.empty()) return;

	ASSERT(quad_pos.x >= 0);
	ASSERT(quad_pos.y >= 0);
	ASSERT(m_splatmap->format == gpu::TextureFormat::RGBA8);

	PROFILE_FUNCTION();
	
	const Texture* splat_map = m_splatmap;

	const float grass_quad_size_hm_space = GRASS_QUAD_SIZE / m_scale.x;
	const Vec2 quad_size = {
		minimum(grass_quad_size_hm_space, m_heightmap->width - quad_pos.x),
		minimum(grass_quad_size_hm_space, m_heightmap->height - quad_pos.y)
	};

	struct { float x, y; void* type; } hashed_patch = { quad_pos.x, quad_pos.y, patch.m_type };
	const u32 hash = crc32(&hashed_patch, sizeof(hashed_patch));
	seedRandom(hash);
	const int max_idx = splat_map->width * splat_map->height;

	const Vec2 step = quad_size * (1 / (float)patch.m_type->m_density);
	for (float dy = 0; dy < quad_size.y; dy += step.y)
	{
		for (float dx = 0; dx < quad_size.x; dx += step.x)
		{
			const Vec2 sm_pos(
				(dx + quad_pos.x) / m_width * splat_map->width,
				(dy + quad_pos.y) / m_height * splat_map->height
			);

			int tx = int(sm_pos.x) + int(sm_pos.y) * splat_map->width;
			tx = clamp(tx, 0, max_idx - 1);

			const u32 pixel_value = ((const u32*)&splat_map->data.getData()[0])[tx];

			const int ground_mask = (pixel_value >> 16) & 0xffff;
			if ((ground_mask & (1 << patch.m_type->m_idx)) == 0) continue;

			const float x = (quad_pos.x + dx + step.x * randFloat(-0.5f, 0.5f)) * m_scale.x;
			const float z = (quad_pos.y + dy + step.y * randFloat(-0.5f, 0.5f)) * m_scale.z;
			const Vec3 instance_rel_pos(x, getHeight(x, z), z);
			Quat instance_rel_rot;
			
			switch (patch.m_type->m_rotation_mode)
			{
				case GrassType::RotationMode::Y_UP:
				{
					instance_rel_rot = Quat(Vec3(0, 1, 0), randFloat(0, PI * 2));
				}
				break;
				case GrassType::RotationMode::ALL_RANDOM:
				{
					const Vec3 random_axis(randFloat(-1, 1), randFloat(-1, 1), randFloat(-1, 1));
					const float random_angle = randFloat(0, PI * 2);
					instance_rel_rot = Quat(random_axis.normalized(), random_angle);
				}
				break;
				case GrassType::RotationMode::ALIGN_WITH_NORMAL:
				{
					const Vec3 normal = getNormal(x, z);
					const Quat random_base(Vec3(0, 1, 0), randFloat(0, PI * 2));
					const Quat to_normal = Quat::vec3ToVec3({0, 1, 0}, normal);
					instance_rel_rot = to_normal * random_base;
				}
				break;
				default: ASSERT(false); break;
			}

			GrassPatch::InstanceData& instance_data = patch.instance_data.emplace();
			instance_data.pos_scale.set(instance_rel_pos, randFloat(0.75f, 1.25f));
			instance_data.rot = instance_rel_rot;
			instance_data.normal = Vec4(getNormal(x, z), 0);
		}
	}
}


void Terrain::updateGrass(int view, const DVec3& camera_pos)
{
	PROFILE_FUNCTION();
	if (!m_splatmap) return;

	Universe& universe = m_scene.getUniverse();

	while (m_last_camera_position.size() <= view) m_last_camera_position.push({ DBL_MAX, DBL_MAX, DBL_MAX });

	if ((m_last_camera_position[view] - camera_pos).length() <= FLT_MIN && !m_force_grass_update) return;
	m_last_camera_position[view] = camera_pos;

	m_force_grass_update = false;
	const RigidTransform terrain_tr = universe.getTransform(m_entity).getRigidPart();
	const Vec3 local_camera_pos = terrain_tr.rot.conjugated() * (camera_pos - terrain_tr.pos).toFloat();
	float cx = (int)(local_camera_pos.x / (GRASS_QUAD_SIZE)) * GRASS_QUAD_SIZE;
	float cz = (int)(local_camera_pos.z / (GRASS_QUAD_SIZE)) * GRASS_QUAD_SIZE;
	int grass_distance = 0;
	for (auto& type : m_grass_types)
	{
		grass_distance = maximum(grass_distance, int(type.m_distance / GRASS_QUAD_RADIUS + 0.99f));
	}

	float from_quad_x = cx - grass_distance * GRASS_QUAD_SIZE;
	float from_quad_z = cz - grass_distance * GRASS_QUAD_SIZE;
	float to_quad_x = cx + grass_distance * GRASS_QUAD_SIZE;
	float to_quad_z = cz + grass_distance * GRASS_QUAD_SIZE;

	float old_bounds[4] = {FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX};
	Array<GrassQuad*>& quads = getQuads(view);
	for (int i = quads.size() - 1; i >= 0; --i)
	{
		GrassQuad* quad = quads[i];
		old_bounds[0] = minimum(old_bounds[0], quad->pos.x);
		old_bounds[1] = maximum(old_bounds[1], quad->pos.x);
		old_bounds[2] = minimum(old_bounds[2], quad->pos.z);
		old_bounds[3] = maximum(old_bounds[3], quad->pos.z);
		if (quad->pos.x < from_quad_x || quad->pos.x > to_quad_x || quad->pos.z < from_quad_z ||
			quad->pos.z > to_quad_z)
		{
			LUMIX_DELETE(m_allocator, quads[i]);
			quads.swapAndPop(i);
		}
	}

	from_quad_x = maximum(0.0f, from_quad_x);
	from_quad_z = maximum(0.0f, from_quad_z);

	for (float quad_z = from_quad_z; quad_z <= to_quad_z; quad_z += GRASS_QUAD_SIZE)
	{
		for (float quad_x = from_quad_x; quad_x <= to_quad_x; quad_x += GRASS_QUAD_SIZE)
		{
			if (quad_x >= old_bounds[0] && quad_x <= old_bounds[1] && quad_z >= old_bounds[2] &&
				quad_z <= old_bounds[3])
				continue;

			PROFILE_BLOCK("generate quad");
			GrassQuad* quad = LUMIX_NEW(m_allocator, GrassQuad)(m_allocator);
			quads.push(quad);
			quad->pos.x = quad_x;
			quad->pos.z = quad_z;
			quad->m_patches.reserve(m_grass_types.size());

			float min_y = FLT_MAX;
			float max_y = -FLT_MAX;
			for (auto& grass_type : m_grass_types)
			{
				Model* model = grass_type.m_grass_model;
				if (!model || !model->isReady()) continue;
				GrassPatch& patch = quad->m_patches.emplace(m_allocator);
				patch.m_type = &grass_type;

				generateGrassTypeQuad(patch, terrain_tr, {quad_x / m_scale.x, quad_z / m_scale.z});
				for (auto instance_data : patch.instance_data)
				{
					min_y = minimum(instance_data.pos_scale.y, min_y);
					max_y = maximum(instance_data.pos_scale.y, max_y);
				}
			}

			quad->pos.y = (max_y + min_y) * 0.5f;
			quad->radius = maximum((max_y - min_y) * 0.5f, GRASS_QUAD_SIZE) * SQRT2;

		}
	}
}


void Terrain::grassLoaded(Resource::State, Resource::State, Resource&)
{
	forceGrassUpdate();
}


void Terrain::setMaterial(Material* material)
{
	if (material != m_material) {
		if (m_material) {
			m_material->getResourceManager().unload(*m_material);
			m_material->getObserverCb().unbind<&Terrain::onMaterialLoaded>(this);
		}
		m_material = material;
		if (m_material) {
			m_material->onLoaded<&Terrain::onMaterialLoaded>(this);
		}
	}
	else if(material) {
		material->getResourceManager().unload(*material);
	}
}

void Terrain::deserialize(EntityRef entity, InputMemoryStream& serializer, Universe& universe, RenderScene& scene)
{
	m_entity = entity;
	serializer.read(m_layer_mask);
	const char* path = serializer.readString();
	serializer.read(m_scale.x);
	serializer.read(m_scale.y);
	m_scale.z = m_scale.x;
	setMaterial(scene.getEngine().getResourceManager().load<Material>(Path(path)));
	i32 count;
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
		const char* path = serializer.readString();
		serializer.read(m_grass_types[i].m_density);
		serializer.read(m_grass_types[i].m_distance);
		serializer.read(m_grass_types[i].m_rotation_mode);
		setGrassTypePath(i, Path(path));
	}
	universe.onComponentCreated(m_entity, TERRAIN_HASH, &scene);
}

	
void Terrain::serialize(OutputMemoryStream& serializer)
{
	serializer.write(m_layer_mask);
	serializer.writeString(m_material ? m_material->getPath().c_str() : "");
	serializer.write(m_scale.x);
	serializer.write(m_scale.y);
	serializer.write((i32)m_grass_types.size());
	for(int i = 0; i < m_grass_types.size(); ++i)
	{
		GrassType& type = m_grass_types[i];
		serializer.writeString(type.m_grass_model ? type.m_grass_model->getPath().c_str() : "");
		serializer.write(type.m_density);
		serializer.write(type.m_distance);
		serializer.write(type.m_rotation_mode);
	}
}


TerrainInfo Terrain::getInfo()
{
	if (!m_material || !m_material->isReady()) return {};
	
	TerrainInfo info;
	info.shader = m_material->getShader();
	info.position = m_scene.getUniverse().getPosition(m_entity);
	info.rot = m_scene.getUniverse().getRotation(m_entity);
	info.terrain = this;
	return info;
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
	float inv_scale = 1.0f / m_scale.x;
	int int_x = (int)(x * inv_scale);
	int int_z = (int)(z * inv_scale);
	float dec_x = (x - (int_x * m_scale.x)) * inv_scale;
	float dec_z = (z - (int_z * m_scale.x)) * inv_scale;
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
	const float DIV64K = 1.0f / 65535.0f;
	if (!m_heightmap) return 0;

	Texture* t = m_heightmap;
	ASSERT(t->format == gpu::TextureFormat::R16);
	int idx = clamp(x, 0, m_width) + clamp(z, 0, m_height) * m_width;
	return m_scale.y * DIV64K * ((u16*)t->getData())[idx];
}


void Terrain::setXZScale(float scale) 
{
	m_scale.x = scale;
	m_scale.z = scale;
	forceGrassUpdate();
}


void Terrain::setYScale(float scale)
{
	m_scale.y = scale;
	forceGrassUpdate();
}


void Terrain::setHeight(int x, int z, float h)
{
	if (!m_heightmap) return;

	Texture* t = m_heightmap;
	ASSERT(t->format == gpu::TextureFormat::R16);
	int idx = clamp(x, 0, m_width) + clamp(z, 0, m_height) * m_width;
	((u16*)t->getData())[idx] = (u16)(h * (65535.0f / m_scale.y));
}


RayCastModelHit Terrain::castRay(const DVec3& origin, const Vec3& dir)
{
	RayCastModelHit hit;
	hit.is_hit = false;
	if (!m_heightmap || !m_heightmap->isReady()) return hit;

	const Universe& universe = m_scene.getUniverse();
	const Quat rot = universe.getRotation(m_entity);
	const DVec3 pos = universe.getPosition(m_entity);
	const Vec3 rel_dir = rot.rotate(dir);
	const Vec3 terrain_to_ray = (origin - pos).toFloat();
	const Vec3 rel_origin = rot.conjugated().rotate(terrain_to_ray);

	Vec3 start;
	const Vec3 size(m_width * m_scale.x, m_scale.y * 65535.0f, m_height * m_scale.x);
	if (!getRayAABBIntersection(rel_origin, rel_dir, Vec3::ZERO, size, start)) return hit;

	int hx = (int)(start.x / m_scale.x);
	int hz = (int)(start.z / m_scale.x);

	float next_x = fabs(rel_dir.x) < 0.01f ? hx : ((hx + (rel_dir.x < 0 ? 0 : 1)) * m_scale.x - rel_origin.x) / rel_dir.x;
	float next_z = fabs(rel_dir.z) < 0.01f ? hz : ((hz + (rel_dir.z < 0 ? 0 : 1)) * m_scale.x - rel_origin.z) / rel_dir.z;

	float delta_x = fabsf(rel_dir.x) < 0.01f ? 0 : m_scale.x / fabsf(rel_dir.x);
	float delta_z = fabsf(rel_dir.z) < 0.01f ? 0 : m_scale.z / fabsf(rel_dir.z);
	int step_x = (int)signum(rel_dir.x);
	int step_z = (int)signum(rel_dir.z);

	while (hx >= 0 && hz >= 0 && hx + step_x < m_width && hz + step_z < m_height) {
		float t;
		float x = hx * m_scale.x;
		float z = hz * m_scale.x;
		Vec3 p0(x, getHeight(x, z), z);
		Vec3 p1(x + m_scale.x, getHeight(x + m_scale.x, z), z);
		Vec3 p2(x + m_scale.x, getHeight(x + m_scale.x, z + m_scale.x), z + m_scale.x);
		Vec3 p3(x, getHeight(x, z + m_scale.x), z + m_scale.x);
		if (getRayTriangleIntersection(rel_origin, rel_dir, p0, p1, p2, &t)) {
			hit.is_hit = true;
			hit.origin = origin;
			hit.dir = dir;
			hit.t = t;
			return hit;
		}
		if (getRayTriangleIntersection(rel_origin, rel_dir, p0, p2, p3, &t)) {
			hit.is_hit = true;
			hit.origin = origin;
			hit.dir = dir;
			hit.t = t;
			return hit;
		}
		if (next_x < next_z && step_x != 0) {
			next_x += delta_x;
			hx += step_x;
		}
		else {
			next_z += delta_z;
			hz += step_z;
		}
		if (delta_x == 0 && delta_z == 0) {
			return hit;
		}
	}
	return hit;
}


void Terrain::onMaterialLoaded(Resource::State, Resource::State new_state, Resource&)
{
	PROFILE_FUNCTION();
	if (new_state == Resource::State::READY)
	{
		m_heightmap = m_material->getTextureByName("Heightmap");
		bool is_data_ready = true;
		if (m_heightmap && m_heightmap->getData() == nullptr)
		{
			m_heightmap->addDataReference();
			is_data_ready = false;
		}
		if (m_heightmap)
		{
			m_width = m_heightmap->width;
			m_height = m_heightmap->height;
		}

		m_albedomap = m_material->getTextureByName("Albedo");
		m_splatmap = m_material->getTextureByName("Splatmap");

		if (m_splatmap && !m_splatmap->getData()) {
			m_splatmap->addDataReference();
			is_data_ready = false;
		}
		/*
		Texture* colormap = m_material->getTextureByUniform("u_colormap");
		if (colormap && colormap->getData() == nullptr)
		{
			colormap->addDataReference();
			is_data_ready = false;
		}

		if (is_data_ready)
		{
			LUMIX_DELETE(m_allocator, m_root);
			if (m_heightmap)
			{
				m_width = m_heightmap->width;
				m_height = m_heightmap->height;
				m_root = generateQuadTree((float)m_width);
			}
		}*/
	}
	else
	{
		//LUMIX_DELETE(m_allocator, m_root);
		//m_root = nullptr;
	}
}


} // namespace Lumix