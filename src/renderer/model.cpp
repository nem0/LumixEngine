#include "engine/lumix.h"
#include "renderer/model.h"

#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/fs/file_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/vec.h"
#include "renderer/material.h"
#include "renderer/model_manager.h"
#include "renderer/pose.h"
#include "renderer/renderer.h"

#include <cfloat>
#include <cmath>


namespace Lumix
{


static const ResourceType MATERIAL_TYPE("material");


bool Model::force_keep_skin = false;


Mesh::Mesh(Material* mat,
	int attribute_array_offset,
	int attribute_array_size,
	int indices_offset,
	int index_count,
	const char* name,
	IAllocator& allocator)
	: name(name, allocator)
{
	this->material = mat;
	this->attribute_array_offset = attribute_array_offset;
	this->attribute_array_size = attribute_array_size;
	this->indices_offset = indices_offset;
	this->indices_count = index_count;
	this->instance_idx = -1;
}


void Mesh::set(int attribute_array_offset, int attribute_array_size, int indices_offset, int index_count)
{
	this->attribute_array_offset = attribute_array_offset;
	this->attribute_array_size = attribute_array_size;
	this->indices_offset = indices_offset;
	this->indices_count = index_count;
}


void Mesh::setMaterial(Material* new_material, Model& model, Renderer& renderer)
{
	if (material) material->getResourceManager().unload(*material);
	material = new_material;
	static const int transparent_layer = renderer.getLayer("transparent");
	layer_mask = material->getRenderLayerMask();
	if (material->getRenderLayer() == transparent_layer)
	{
		type = Mesh::RIGID;
	}
	else if (material->getLayersCount() > 0)
	{
		if (model.getBoneCount() > 0)
		{
			type = Mesh::MULTILAYER_SKINNED;
		}
		else
		{
			type = Mesh::MULTILAYER_RIGID;
		}
	}
	else if (model.getBoneCount() > 0) type = Mesh::SKINNED;
	else type = Mesh::RIGID_INSTANCED;
}


Model::Model(const Path& path, ResourceManagerBase& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_bounding_radius()
	, m_allocator(allocator)
	, m_bone_map(m_allocator)
	, m_meshes(m_allocator)
	, m_bones(m_allocator)
	, m_indices(m_allocator)
	, m_vertices(m_allocator)
	, m_skin(m_allocator)
	, m_uvs(m_allocator)
	, m_vertices_handle(BGFX_INVALID_HANDLE)
	, m_indices_handle(BGFX_INVALID_HANDLE)
	, m_first_nonroot_bone_index(0)
	, m_flags(0)
	, m_loading_flags(0)
	, m_renderer(renderer)
{
	if (force_keep_skin) m_loading_flags = (u32)LoadingFlags::KEEP_SKIN;
	m_lods[0] = { 0, -1, FLT_MAX };
	m_lods[1] = { 0, -1, FLT_MAX };
	m_lods[2] = { 0, -1, FLT_MAX };
	m_lods[3] = { 0, -1, FLT_MAX };
}


Model::~Model()
{
	ASSERT(isEmpty());
}


static inline Vec3 evaluateSkin(Vec3& p, Model::Skin s, const Matrix* matrices)
{
	Matrix m = matrices[s.indices[0]] * s.weights.x + matrices[s.indices[1]] * s.weights.y +
			   matrices[s.indices[2]] * s.weights.z + matrices[s.indices[3]] * s.weights.w;

	return m.transformPoint(p);
}


static inline void computeSkinMatrices(const Pose& pose, const Model& model, Matrix* matrices)
{
	for (int i = 0; i < pose.count; ++i)
	{
		auto& bone = model.getBone(i);
		RigidTransform tmp = { pose.positions[i], pose.rotations[i] };
		matrices[i] = (tmp * bone.inv_bind_transform).toMatrix();
	}
}


RayCastModelHit Model::castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform, const Pose* pose)
{
	RayCastModelHit hit;
	hit.m_is_hit = false;
	if (!isReady()) return hit;

	Matrix inv = model_transform;
	inv.inverse();
	Vec3 local_origin = inv.transformPoint(origin);
	Vec3 local_dir = static_cast<Vec3>(inv * Vec4(dir.x, dir.y, dir.z, 0));

	const Array<Vec3>& vertices = m_vertices;
	const Array<Skin>& skin = m_skin;
	u16* indices16 = (u16*)&m_indices[0];
	u32* indices32 = (u32*)&m_indices[0];
	int vertex_offset = 0;
	bool is16 = m_flags & (u32)Model::Flags::INDICES_16BIT;
	Matrix matrices[256];
	ASSERT(!pose || pose->count <= lengthOf(matrices));
	bool is_skinned = pose && !m_skin.empty() && pose->count <= lengthOf(matrices);
	if (is_skinned)
	{
		computeSkinMatrices(*pose, *this, matrices);
	}

	for (int mesh_index = m_lods[0].from_mesh; mesh_index <= m_lods[0].to_mesh; ++mesh_index)
	{
		int indices_end = m_meshes[mesh_index].indices_offset + m_meshes[mesh_index].indices_count;
		for(int i = m_meshes[mesh_index].indices_offset; i < indices_end; i += 3)
		{
			Vec3 p0, p1, p2;
			if (is16)
			{
				p0 = vertices[vertex_offset + indices16[i]];
				p1 = vertices[vertex_offset + indices16[i + 1]];
				p2 = vertices[vertex_offset + indices16[i + 2]];
				if (is_skinned)
				{
					p0 = evaluateSkin(p0, skin[vertex_offset + indices16[i]], matrices);
					p1 = evaluateSkin(p1, skin[vertex_offset + indices16[i + 1]], matrices);
					p2 = evaluateSkin(p2, skin[vertex_offset + indices16[i + 2]], matrices);
				}
			}
			else
			{
				p0 = vertices[vertex_offset + indices32[i]];
				p1 = vertices[vertex_offset + indices32[i + 1]];
				p2 = vertices[vertex_offset + indices32[i + 2]];
				if (is_skinned)
				{
					p0 = evaluateSkin(p0, skin[vertex_offset + indices32[i]], matrices);
					p1 = evaluateSkin(p1, skin[vertex_offset + indices32[i + 1]], matrices);
					p2 = evaluateSkin(p2, skin[vertex_offset + indices32[i + 2]], matrices);
				}
			}


			Vec3 normal = crossProduct(p1 - p0, p2 - p0);
			float q = dotProduct(normal, local_dir);
			if (q == 0)	continue;

			float d = -dotProduct(normal, p0);
			float t = -(dotProduct(normal, local_origin) + d) / q;
			if (t < 0) continue;

			Vec3 hit_point = local_origin + local_dir * t;

			Vec3 edge0 = p1 - p0;
			Vec3 VP0 = hit_point - p0;
			if (dotProduct(normal, crossProduct(edge0, VP0)) < 0) continue;

			Vec3 edge1 = p2 - p1;
			Vec3 VP1 = hit_point - p1;
			if (dotProduct(normal, crossProduct(edge1, VP1)) < 0) continue;

			Vec3 edge2 = p0 - p2;
			Vec3 VP2 = hit_point - p2;
			if (dotProduct(normal, crossProduct(edge2, VP2)) < 0) continue;

			if (!hit.m_is_hit || hit.m_t > t)
			{
				hit.m_is_hit = true;
				hit.m_t = t;
				hit.m_mesh = &m_meshes[mesh_index];
			}
		}
		vertex_offset += m_meshes[mesh_index].attribute_array_size / m_vertex_decl.getStride();
	}
	hit.m_origin = origin;
	hit.m_dir = dir;
	return hit;
}


void Model::getRelativePose(Pose& pose)
{
	ASSERT(pose.count == getBoneCount());
	Vec3* pos = pose.positions;
	Quat* rot = pose.rotations;
	for (int i = 0, c = getBoneCount(); i < c; ++i)
	{
		pos[i] = m_bones[i].relative_transform.pos;
		rot[i] = m_bones[i].relative_transform.rot;
	}
	pose.is_absolute = false;
}


void Model::getPose(Pose& pose)
{
	ASSERT(pose.count == getBoneCount());
	Vec3* pos = pose.positions;
	Quat* rot = pose.rotations;
	for (int i = 0, c = getBoneCount(); i < c; ++i)
	{
		pos[i] = m_bones[i].transform.pos;
		rot[i] = m_bones[i].transform.rot;
	}
	pose.is_absolute = true;
}


bool Model::parseVertexDecl(FS::IFile& file, bgfx::VertexDecl* vertex_decl)
{
	vertex_decl->begin();

	u32 attribute_count;
	file.read(&attribute_count, sizeof(attribute_count));

	for (u32 i = 0; i < attribute_count; ++i)
	{
		char tmp[50];
		u32 len;
		file.read(&len, sizeof(len));
		if (len > sizeof(tmp) - 1)
		{
			return false;
		}
		file.read(tmp, len);
		tmp[len] = '\0';

		if (equalStrings(tmp, "in_position"))
		{
			vertex_decl->add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
		}
		else if (equalStrings(tmp, "in_colors"))
		{
			vertex_decl->add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, false);
		}
		else if (equalStrings(tmp, "in_tex_coords"))
		{
			vertex_decl->add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
		}
		else if (equalStrings(tmp, "in_normal"))
		{
			vertex_decl->add(bgfx::Attrib::Normal, 4, bgfx::AttribType::Uint8, true, true);
		}
		else if (equalStrings(tmp, "in_tangents"))
		{
			vertex_decl->add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Uint8, true, true);
		}
		else if (equalStrings(tmp, "in_weights"))
		{
			vertex_decl->add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float);
		}
		else if (equalStrings(tmp, "in_indices"))
		{
			vertex_decl->add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Int16, false, true);
		}
		else
		{
			ASSERT(false);
			return false;
		}

		u32 type;
		file.read(&type, sizeof(type));
	}

	vertex_decl->end();
	return true;
}


bool Model::parseVertexDeclEx(FS::IFile& file, bgfx::VertexDecl* vertex_decl)
{
	vertex_decl->begin();

	u32 attribute_count;
	file.read(&attribute_count, sizeof(attribute_count));

	for (u32 i = 0; i < attribute_count; ++i)
	{
		i32 attr;
		file.read(&attr, sizeof(attr));

		if (attr == (i32)Attrs::Position)
		{
			vertex_decl->add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
		}
		else if (attr == (i32)Attrs::Color0)
		{
			vertex_decl->add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, false);
		}
		else if (attr == (i32)Attrs::TexCoord0)
		{
			vertex_decl->add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
		}
		else if (attr == (i32)Attrs::Normal)
		{
			vertex_decl->add(bgfx::Attrib::Normal, 4, bgfx::AttribType::Uint8, true, true);
		}
		else if (attr == (i32)Attrs::Tangent)
		{
			vertex_decl->add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Uint8, true, true);
		}
		else if (attr == (i32)Attrs::Weight)
		{
			vertex_decl->add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float);
		}
		else if (attr == (i32)Attrs::Indices)
		{
			vertex_decl->add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Int16, false, true);
		}
		else
		{
			ASSERT(false);
			return false;
		}
	}

	vertex_decl->end();
	return true;
}


void Model::create(const bgfx::VertexDecl& vertex_decl,
	Material* material,
	const int* indices_data,
	int indices_size,
	const void* attributes_data,
	int attributes_size)
{
	ASSERT(!bgfx::isValid(m_vertices_handle));
	m_vertex_decl = vertex_decl;
	m_vertices_handle = bgfx::createVertexBuffer(bgfx::copy(attributes_data, attributes_size), vertex_decl);

	ASSERT(!bgfx::isValid(m_indices_handle));
	auto* mem = bgfx::copy(indices_data, indices_size);
	m_indices_handle = bgfx::createIndexBuffer(mem, BGFX_BUFFER_INDEX32);
	m_meshes.emplace(material, 0, attributes_size, 0, indices_size / int(sizeof(int)), "default", m_allocator);

	Model::LOD lod;
	lod.distance = FLT_MAX;
	lod.from_mesh = 0;
	lod.to_mesh = 0;
	m_lods[0] = lod;

	m_indices.resize(indices_size);
	copyMemory(&m_indices[0], indices_data, indices_size);

	m_vertices.resize(attributes_size / vertex_decl.getStride());
	m_uvs.resize(m_vertices.size());
	if (m_loading_flags & (u32)LoadingFlags::KEEP_SKIN)
	{
		m_skin.resize(m_vertices.size());
	}
	computeRuntimeData((const u8*)attributes_data, true);

	onCreated(State::READY);
}


void Model::onBeforeReady()
{
	static const int transparent_layer = m_renderer.getLayer("transparent");
	for (Mesh& mesh : m_meshes)
	{
		mesh.layer_mask = mesh.material->getRenderLayerMask();
		if (mesh.material->getRenderLayer() == transparent_layer)
		{
			mesh.type = Mesh::RIGID;
		}
		else if (mesh.material->getLayersCount() > 0)
		{
			if (getBoneCount() > 0)
			{
				mesh.type = Mesh::MULTILAYER_SKINNED;
			}
			else
			{
				mesh.type = Mesh::MULTILAYER_RIGID;
			}
		}
		else if (getBoneCount() > 0) mesh.type = Mesh::SKINNED;
		else mesh.type = Mesh::RIGID_INSTANCED;
	}
}


void Model::setKeepSkin()
{
	if (m_loading_flags & (u32)LoadingFlags::KEEP_SKIN) return;
	m_loading_flags = m_loading_flags | (u32)LoadingFlags::KEEP_SKIN;
	if (isReady()) m_resource_manager.reload(*this);
}


void Model::computeRuntimeData(const u8* vertices, bool compute_bounding_shape)
{
	int index = 0;
	float bounding_radius_squared = 0;
	Vec3 min_vertex(0, 0, 0);
	Vec3 max_vertex(0, 0, 0);

	int vertex_size = m_vertex_decl.getStride();
	int position_attribute_offset = m_vertex_decl.getOffset(bgfx::Attrib::Position);
	int uv_attribute_offset = m_vertex_decl.getOffset(bgfx::Attrib::TexCoord0);
	int weights_attribute_offset = m_vertex_decl.getOffset(bgfx::Attrib::Weight);
	int bone_indices_attribute_offset = m_vertex_decl.getOffset(bgfx::Attrib::Indices);
	bool keep_skin = m_loading_flags & (u32)LoadingFlags::KEEP_SKIN;
	keep_skin = keep_skin && weights_attribute_offset >= 0 && bone_indices_attribute_offset >= 0;
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		int mesh_vertex_count = m_meshes[i].attribute_array_size / m_vertex_decl.getStride();
		int mesh_attributes_array_offset = m_meshes[i].attribute_array_offset;
		for (int j = 0; j < mesh_vertex_count; ++j)
		{
			int offset = mesh_attributes_array_offset + j * vertex_size;
			if (keep_skin)
			{
				m_skin[index].weights = *(const Vec4*)&vertices[offset + weights_attribute_offset];
				copyMemory(m_skin[index].indices,
					&vertices[offset + bone_indices_attribute_offset],
					sizeof(m_skin[index].indices));
			}
			m_vertices[index] = *(const Vec3*)&vertices[offset + position_attribute_offset];
			m_uvs[index] = *(const Vec2*)&vertices[offset + uv_attribute_offset];
			float sq_len = m_vertices[index].squaredLength();
			bounding_radius_squared = Math::maximum(bounding_radius_squared, sq_len > 0 ? sq_len : 0);
			min_vertex.x = Math::minimum(min_vertex.x, m_vertices[index].x);
			min_vertex.y = Math::minimum(min_vertex.y, m_vertices[index].y);
			min_vertex.z = Math::minimum(min_vertex.z, m_vertices[index].z);
			max_vertex.x = Math::maximum(max_vertex.x, m_vertices[index].x);
			max_vertex.y = Math::maximum(max_vertex.y, m_vertices[index].y);
			max_vertex.z = Math::maximum(max_vertex.z, m_vertices[index].z);
			++index;
		}
	}

	if (compute_bounding_shape)
	{
		m_bounding_radius = sqrt(bounding_radius_squared);
		m_aabb = AABB(min_vertex, max_vertex);
	}
}


bool Model::parseGeometry(FS::IFile& file, FileVersion version)
{
	i32 indices_count = 0;
	file.read(&indices_count, sizeof(indices_count));
	if (indices_count <= 0) return false;

	int index_size = (m_flags & (u32)Model::Flags::INDICES_16BIT) ? 2 : 4;
	m_indices.resize(indices_count * index_size);
	file.read(&m_indices[0], index_size * indices_count);

	i32 vertices_size = 0;
	file.read(&vertices_size, sizeof(vertices_size));
	if (vertices_size <= 0) return false;

	ASSERT(!bgfx::isValid(m_vertices_handle));
	const bgfx::Memory* vertices_mem = bgfx::alloc(vertices_size);
	file.read(vertices_mem->data, vertices_size);
	m_vertices_handle = bgfx::createVertexBuffer(vertices_mem, m_vertex_decl);
	if (!bgfx::isValid(m_vertices_handle)) return false;

	ASSERT(!bgfx::isValid(m_indices_handle));
	int indices_size = index_size * indices_count;
	const bgfx::Memory* mem = bgfx::copy(&m_indices[0], indices_size);
	m_indices_handle = bgfx::createIndexBuffer(mem, index_size == 4 ? BGFX_BUFFER_INDEX32 : 0);
	if (!bgfx::isValid(m_indices_handle))
	{
		bgfx::destroy(m_vertices_handle);
		return false;
	}

	int vertex_count = 0;
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		vertex_count += m_meshes[i].attribute_array_size / m_vertex_decl.getStride();
	}
	m_vertices.resize(vertex_count);
	m_uvs.resize(vertex_count);
	if (m_loading_flags & (u32)LoadingFlags::KEEP_SKIN)
	{
		m_skin.resize(m_vertices.size());
	}

	if (version > FileVersion::BOUNDING_SHAPES_PRECOMPUTED)
	{
		file.read(&m_bounding_radius, sizeof(m_bounding_radius));
		file.read(&m_aabb, sizeof(m_aabb));
	}

	computeRuntimeData(vertices_mem->data, version <= FileVersion::BOUNDING_SHAPES_PRECOMPUTED);

	return true;
}

bool Model::parseBones(FS::IFile& file)
{
	int bone_count;
	file.read(&bone_count, sizeof(bone_count));
	if (bone_count < 0) return false;
	if (bone_count > Bone::MAX_COUNT)
	{
		g_log_warning.log("Renderer") << "Model " << getPath().c_str() << " has too many bones.";
		return false;
	}

	m_bones.reserve(bone_count);
	for (int i = 0; i < bone_count; ++i)
	{
		Model::Bone& b = m_bones.emplace(m_allocator);
		int len;
		file.read(&len, sizeof(len));
		char tmp[MAX_PATH_LENGTH];
		if (len >= MAX_PATH_LENGTH)
		{
			return false;
		}
		file.read(tmp, len);
		tmp[len] = 0;
		b.name = tmp;
		m_bone_map.insert(crc32(b.name.c_str()), m_bones.size() - 1);
		file.read(&len, sizeof(len));
		if (len >= MAX_PATH_LENGTH)
		{
			return false;
		}
		if(len > 0)
		{
			file.read(tmp, len);
			tmp[len] = 0;
			b.parent = tmp;
		}
		else
		{
			b.parent = "";
		}
		file.read(&b.transform.pos.x, sizeof(float) * 3);
		file.read(&b.transform.rot.x, sizeof(float) * 4);
	}
	m_first_nonroot_bone_index = -1;
	for (int i = 0; i < bone_count; ++i)
	{
		Model::Bone& b = m_bones[i];
		if (b.parent.length() == 0)
		{
			if (m_first_nonroot_bone_index != -1)
			{
				g_log_error.log("Renderer") << "Invalid skeleton in " << getPath().c_str();
				return false;
			}
			b.parent_idx = -1;
		}
		else
		{
			b.parent_idx = getBoneIdx(b.parent.c_str());
			if (b.parent_idx > i || b.parent_idx < 0)
			{
				g_log_error.log("Renderer") << "Invalid skeleton in " << getPath().c_str();
				return false;
			}
			if (m_first_nonroot_bone_index == -1)
			{
				m_first_nonroot_bone_index = i;
			}
		}
	}

	for (int i = 0; i < m_bones.size(); ++i)
	{
		m_bones[i].inv_bind_transform = m_bones[i].transform.inverted();
	}

	for (int i = 0; i < m_bones.size(); ++i)
	{
		int p = m_bones[i].parent_idx;
		if (p >= 0)
		{
			m_bones[i].relative_transform = m_bones[p].inv_bind_transform * m_bones[i].transform;
		}
		else
		{
			m_bones[i].relative_transform = m_bones[i].transform;
		}
	}
	return true;
}

int Model::getBoneIdx(const char* name)
{
	for (int i = 0, c = m_bones.size(); i < c; ++i)
	{
		if (m_bones[i].name == name)
		{
			return i;
		}
	}
	return -1;
}

bool Model::parseMeshes(FS::IFile& file, FileVersion version)
{
	int object_count = 0;
	file.read(&object_count, sizeof(object_count));
	if (object_count <= 0) return false;

	m_meshes.reserve(object_count);
	char model_dir[MAX_PATH_LENGTH];
	PathUtils::getDir(model_dir, MAX_PATH_LENGTH, getPath().c_str());
	for (int i = 0; i < object_count; ++i)
	{
		i32 str_size;
		file.read(&str_size, sizeof(str_size));
		char material_name[MAX_PATH_LENGTH];
		file.read(material_name, str_size);
		if (str_size >= MAX_PATH_LENGTH) return false;

		material_name[str_size] = 0;

		char material_path[MAX_PATH_LENGTH];
		copyString(material_path, model_dir);
		catString(material_path, material_name);
		catString(material_path, ".mat");

		auto* material_manager = m_resource_manager.getOwner().get(MATERIAL_TYPE);
		Material* material = static_cast<Material*>(material_manager->load(Path(material_path)));

		i32 attribute_array_offset = 0;
		file.read(&attribute_array_offset, sizeof(attribute_array_offset));
		i32 attribute_array_size = 0;
		file.read(&attribute_array_size, sizeof(attribute_array_size));
		i32 indices_offset = 0;
		file.read(&indices_offset, sizeof(indices_offset));
		i32 mesh_tri_count = 0;
		file.read(&mesh_tri_count, sizeof(mesh_tri_count));

		file.read(&str_size, sizeof(str_size));
		if (str_size >= MAX_PATH_LENGTH)
		{
			material_manager->unload(*material);
			return false;
		}

		char mesh_name[MAX_PATH_LENGTH];
		mesh_name[str_size] = 0;
		file.read(mesh_name, str_size);

		if (version <= FileVersion::SINGLE_VERTEX_DECL)
		{
			bgfx::VertexDecl vertex_decl;
			parseVertexDecl(file, &vertex_decl);
			if (i != 0 && m_vertex_decl.m_hash != vertex_decl.m_hash)
			{
				g_log_error.log("Renderer") << "Model " << getPath().c_str()
					<< " contains meshes with different vertex declarations.";
			}
			if(i == 0) m_vertex_decl = vertex_decl;
		}

		m_meshes.emplace(material,
						 attribute_array_offset,
						 attribute_array_size,
						 indices_offset,
						 mesh_tri_count * 3,
						 mesh_name,
						 m_allocator);
		addDependency(*material);
	}
	return true;
}


bool Model::parseLODs(FS::IFile& file)
{
	i32 lod_count;
	file.read(&lod_count, sizeof(lod_count));
	if (lod_count <= 0 || lod_count > lengthOf(m_lods))
	{
		return false;
	}
	for (int i = 0; i < lod_count; ++i)
	{
		file.read(&m_lods[i].to_mesh, sizeof(m_lods[i].to_mesh));
		file.read(&m_lods[i].distance, sizeof(m_lods[i].distance));
		m_lods[i].from_mesh = i > 0 ? m_lods[i - 1].to_mesh + 1 : 0;
	}
	return true;
}


bool Model::load(FS::IFile& file)
{
	PROFILE_FUNCTION();
	FileHeader header;
	file.read(&header, sizeof(header));

	if (header.magic != FILE_MAGIC)
	{
		g_log_warning.log("Renderer") << "Corrupted model " << getPath().c_str();
		return false;
	}

	if(header.version > (u32)FileVersion::LATEST)
	{
		g_log_warning.log("Renderer") << "Unsupported version of model " << getPath().c_str();
		return false;
	}

	m_flags = 0;
	if(header.version > (u32)FileVersion::WITH_FLAGS)
	{
		file.read(&m_flags, sizeof(m_flags));
	}

	if (header.version > (u32)FileVersion::SINGLE_VERTEX_DECL) parseVertexDeclEx(file, &m_vertex_decl);

	if (parseMeshes(file, (FileVersion)header.version)
		&& parseGeometry(file, (FileVersion)header.version)
		&& parseBones(file)
		&& parseLODs(file))
	{
		m_size = file.size();
		return true;
	}

	g_log_warning.log("Renderer") << "Error loading model " << getPath().c_str();
	return false;
}


static Vec3 getBonePosition(Model* model, int bone_index)
{
	return model->getBone(bone_index).transform.pos;
}


static int getBoneParent(Model* model, int bone_index)
{
	return model->getBone(bone_index).parent_idx;
}


void Model::registerLuaAPI(lua_State* L)
{
	#define REGISTER_FUNCTION(F)\
		do { \
			auto f = &LuaWrapper::wrapMethod<Model, decltype(&Model::F), &Model::F>; \
			LuaWrapper::createSystemFunction(L, "Model", #F, f); \
		} while(false) \

	REGISTER_FUNCTION(getBoneCount);

	#undef REGISTER_FUNCTION
	

	#define REGISTER_FUNCTION(F)\
		do { \
			auto f = &LuaWrapper::wrap<decltype(&F), &F>; \
			LuaWrapper::createSystemFunction(L, "Model", #F, f); \
		} while(false) \

	REGISTER_FUNCTION(getBonePosition);
	REGISTER_FUNCTION(getBoneParent);

	#undef REGISTER_FUNCTION
}


void Model::unload(void)
{
	auto* material_manager = m_resource_manager.getOwner().get(MATERIAL_TYPE);
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		removeDependency(*m_meshes[i].material);
		material_manager->unload(*m_meshes[i].material);
	}
	m_meshes.clear();
	m_bones.clear();
	m_uvs.clear();
	m_vertices.clear();

	if(bgfx::isValid(m_vertices_handle)) bgfx::destroy(m_vertices_handle);
	if(bgfx::isValid(m_indices_handle)) bgfx::destroy(m_indices_handle);
	m_indices_handle = BGFX_INVALID_HANDLE;
	m_vertices_handle = BGFX_INVALID_HANDLE;
}


} // ~namespace Lumix
