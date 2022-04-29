#include "terrain.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "engine/universe.h"


namespace Lumix
{


static const float GRASS_QUAD_SIZE = 10.0f;
static const float GRASS_QUAD_RADIUS = GRASS_QUAD_SIZE * 0.7072f;
static const ComponentType TERRAIN_HASH = reflection::getComponentType("terrain");

struct Sample
{
	Vec3 pos;
	float u, v;
};

void Terrain::createGrass(const Vec2& center, u32 frame) {
	PROFILE_FUNCTION();
	if (m_is_grass_dirty) {
		for (GrassType& type : m_grass_types) {
			for (const GrassQuad& quad : type.m_quads) {
				m_renderer.destroy(quad.instances);
			}
			type.m_quads.clear();
		}
		m_is_grass_dirty = false;
	}

	if (!m_heightmap) return;
	if (!m_heightmap->isReady()) return;
	if (!m_splatmap) return;
	if (!m_splatmap->isReady()) return;

	for (GrassType& type : m_grass_types) {
		HashMap<u64, GrassQuad>& quads = type.m_quads;
		quads.eraseIf([&](const GrassQuad& q){
			if (q.last_used_frame < frame - 3) {
				m_renderer.destroy(q.instances);
				return true;
			}
			return false;
		});
	}

	for (u32 type_idx = 0; type_idx < (u32)m_grass_types.size(); ++type_idx) {
		Terrain::GrassType& type = m_grass_types[type_idx];
		if (type.m_spacing <= 0) continue;

		HashMap<u64, GrassQuad>& quads = type.m_quads;
		const Vec2 half_extents(type.m_distance);
		const Vec2 size(type.m_distance * 2);
		const Vec2 quad_size(type.m_spacing * 32);
		const IVec2 ij = maximum(IVec2((center - half_extents) / quad_size), IVec2(0));
		
		struct Instance {
			Vec3 position;
			float scale;
			Quat rotation;
		};

		Array<Instance> instances(m_allocator);
		
		const u32 cols = 1 + u32(size.x / quad_size.x);
		const u32 rows = 1 + u32(size.y / quad_size.y);

		for (u32 j = ij.y; j < ij.y + rows; ++j) {
			for (u32 i = ij.x; i < ij.x + cols; ++i) {
				
				const u64 key = (u64(i) << 32) | u32(j);
				auto quad_iter = quads.find(key);

				if (quad_iter.isValid()) {
					quad_iter.value().last_used_frame = frame;
					continue;
				}

				PROFILE_BLOCK("create grass quad")
				const Vec2 from = Vec2((float)i, (float)j) * quad_size;

				instances.clear();
				AABB aabb(Vec3(FLT_MAX), Vec3(-FLT_MAX));
				RandomGenerator rg(i, j);

				for (u32 k = 0; k < 1024; ++k) {
					const Vec2 pn = Vec2(rg.randFloat(), rg.randFloat());
					Vec4 p;
					p.x = from.x + pn.x * quad_size.x;
					p.z = from.y + pn.y * quad_size.y;
					const u32 splat = m_splatmap->getPixelNearest(u32(p.x / m_scale.x), u32(p.z / m_scale.x));
					if ((splat >> 16) & (1 << type_idx)) {
						p.y = getHeight(p.x, p.z);
						p.w = rg.randFloat(0.7f, 1.f);
						Instance& inst = instances.emplace();
						inst.position = p.xyz();
						inst.scale = p.w;
						switch (type.m_rotation_mode) {
							case GrassType::RotationMode::Y_UP: {
								const float angle = rg.randFloat();
								inst.rotation = Quat(0, sinf(angle * PI), 0, cosf(angle * PI));
								break;
							}
							case GrassType::RotationMode::ALL_RANDOM: {
								const Vec3 axis = normalize(Vec3(rg.randFloat(), rg.randFloat(), rg.randFloat()) * 2.f - 1.f);
								inst.rotation = Quat(axis, rg.randFloat() * 2 * PI);
								break;
							}
							default: 
								inst.rotation = Quat::IDENTITY;
								ASSERT(false);
								break;
						}
						instances.push(inst);
						aabb.addPoint(p.xyz());
					}
				}

				GrassQuad& quad = quads.insert(key);
				quad.aabb = aabb;
				quad.ij = IVec2(i, j);
				quad.type = type_idx;
				quad.last_used_frame = frame;
				if (!instances.empty()) {
					const Renderer::MemRef mem = m_renderer.copy(instances.begin(), instances.byte_size());
					quad.instances = m_renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE);
					quad.instances_count = instances.size();
				}
			}
		}
	}
}

Terrain::Terrain(Renderer& renderer, EntityPtr entity, RenderScene& scene, IAllocator& allocator)
	: m_material(nullptr)
	, m_albedomap(nullptr)
	, m_heightmap(nullptr)
	, m_splatmap(nullptr)
	, m_width(0)
	, m_height(0)
	, m_layer_mask(1)
	, m_scale(1, 100, 1)
	, m_entity(entity)
	, m_scene(scene)
	, m_allocator(allocator)
	, m_grass_types(m_allocator)
	, m_renderer(renderer)
{
}

Terrain::GrassType::~GrassType()
{
	if (m_grass_model)
	{
		m_grass_model->decRefCount();
	}
}

Terrain::~Terrain()
{
	for (const GrassType& type : m_grass_types) {
		for (const GrassQuad& quad : type.m_quads) {
			m_renderer.destroy(quad.instances);
		}
	}
	setMaterial(nullptr);
}

Terrain::GrassType::GrassType(GrassType&& rhs)
	: m_grass_model(rhs.m_grass_model)
	, m_terrain(rhs.m_terrain)
	, m_spacing(rhs.m_spacing)
	, m_distance(rhs.m_distance)
	, m_idx(rhs.m_idx)
	, m_rotation_mode(rhs.m_rotation_mode)
	, m_quads(rhs.m_quads.move())
{
	rhs.m_grass_model = nullptr;
}

Terrain::GrassType::GrassType(Terrain& terrain)
	: m_terrain(terrain)
	, m_quads(terrain.m_allocator)
{
	m_grass_model = nullptr;
	m_spacing = 1.f;
	m_distance = 50;
}


void Terrain::addGrassType(int index)
{
	if(index < 0)
	{
		int idx = m_grass_types.size();
		m_grass_types.emplace(*this).m_idx = idx;
	}
	else
	{
		GrassType type(*this);
		type.m_idx = index;
		m_grass_types.insert(index, static_cast<GrassType&&>(type));
	}
}


void Terrain::removeGrassType(int index)
{
	m_grass_types.erase(index);
}


void Terrain::setGrassTypeSpacing(int index, float spacing)
{
	GrassType& type = m_grass_types[index];
	type.m_spacing = spacing;
}


Terrain::GrassType::RotationMode Terrain::getGrassTypeRotationMode(int index) const
{
	const GrassType& type = m_grass_types[index];
	return type.m_rotation_mode;
}


void Terrain::setGrassTypeRotationMode(int index, Terrain::GrassType::RotationMode mode)
{
	m_grass_types[index].m_rotation_mode = mode;
}


float Terrain::getGrassTypeSpacing(int index) const
{
	const GrassType& type = m_grass_types[index];
	return type.m_spacing;
}


void Terrain::setGrassTypeDistance(int index, float distance)
{
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
	GrassType& type = m_grass_types[index];
	if (type.m_grass_model)
	{
		type.m_grass_model->decRefCount();
		type.m_grass_model = nullptr;
	}
	if (!path.isEmpty())
	{
		type.m_grass_model = m_scene.getEngine().getResourceManager().load<Model>(path);
	}
}


void Terrain::setMaterial(Material* material)
{
	if (material != m_material) {
		if (m_material) {
			m_material->decRefCount();
			m_material->getObserverCb().unbind<&Terrain::onMaterialLoaded>(this);
		}
		m_material = material;
		if (m_material) {
			m_material->onLoaded<&Terrain::onMaterialLoaded>(this);
		}
	}
	else if(material) {
		material->decRefCount();
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
		serializer.read(m_grass_types[i].m_spacing);
		m_grass_types[i].m_spacing = clamp(m_grass_types[i].m_spacing, 0.1f, 9000.f);
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
		serializer.write(type.m_spacing);
		serializer.write(type.m_distance);
		serializer.write(type.m_rotation_mode);
	}
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
		return normalize(cross(Vec3(m_scale.x, h2 - h0, m_scale.x), Vec3(m_scale.x, h1 - h0, 0)));
	}
	else
	{
		float h0 = getHeight(int_x, int_z);
		float h1 = getHeight(int_x + 1, int_z + 1);
		float h2 = getHeight(int_x, int_z + 1);
		return normalize(cross(Vec3(0, h2 - h0, m_scale.x), Vec3(m_scale.x, h1 - h0, m_scale.x)));
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
	const float DIV255 = 1.0f / 255.0f;
	if (!m_heightmap) return 0;

	Texture* t = m_heightmap;
	int idx = clamp(x, 0, m_width - 1) + clamp(z, 0, m_height - 1) * m_width;
	if (t->format == gpu::TextureFormat::R16) {
		return m_scale.y * DIV64K * ((u16*)t->getData())[idx];
	}
	if (t->format == gpu::TextureFormat::RGBA8) {
		return m_scale.y * DIV255 * (((u32*)t->getData())[idx] & 0xff);
	}
	ASSERT(false);
	return 0;
}


void Terrain::setXZScale(float scale) 
{
	m_scale.x = scale;
	m_scale.z = scale;
}


void Terrain::setYScale(float scale)
{
	m_scale.y = scale;
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
	hit.mesh = nullptr;
	if (!m_heightmap || !m_heightmap->isReady()) return hit;

	const Universe& universe = m_scene.getUniverse();
	const Quat rot = universe.getRotation(m_entity);
	const DVec3 pos = universe.getPosition(m_entity);
	const Vec3 rel_dir = rot.rotate(dir);
	const Vec3 terrain_to_ray = Vec3(origin - pos);
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

		m_albedomap = m_material->getTextureByName("Detail albedo");
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