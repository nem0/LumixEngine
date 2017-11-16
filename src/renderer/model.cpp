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
	const bgfx::VertexDecl& vertex_decl,
	const char* name,
	IAllocator& allocator)
	: name(name, allocator)
	, vertex_decl(vertex_decl)
	, material(mat)
	, instance_idx(-1)
	, indices(allocator)
	, vertices(allocator)
	, uvs(allocator)
	, skin(allocator)
{
}


void Mesh::set(const Mesh& rhs)
{
	type = rhs.type;
	indices = rhs.indices;
	vertices = rhs.vertices;
	uvs = rhs.uvs;
	skin = rhs.skin;
	flags = rhs.flags;
	layer_mask = rhs.layer_mask;
	instance_idx = rhs.instance_idx;
	indices_count = rhs.indices_count;
	vertex_decl = rhs.vertex_decl;
	vertex_buffer_handle = rhs.vertex_buffer_handle;
	index_buffer_handle = rhs.index_buffer_handle;
	name = rhs.name;
	// all except material
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
	else if (model.getBoneCount() > 0)
	{
		type = skin.empty() ? Mesh::RIGID_INSTANCED : Mesh::SKINNED;
	}
	else type = Mesh::RIGID_INSTANCED;
}


Model::Model(const Path& path, ResourceManagerBase& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_bounding_radius()
	, m_allocator(allocator)
	, m_bone_map(m_allocator)
	, m_meshes(m_allocator)
	, m_bones(m_allocator)
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


static inline Vec3 evaluateSkin(Vec3& p, Mesh::Skin s, const Matrix* matrices)
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

	Matrix matrices[256];
	ASSERT(!pose || pose->count <= lengthOf(matrices));
	bool is_skinned = false;
	for (int mesh_index = m_lods[0].from_mesh; mesh_index <= m_lods[0].to_mesh; ++mesh_index)
	{
		Mesh& mesh = m_meshes[mesh_index];
		is_skinned = pose && !mesh.skin.empty() && pose->count <= lengthOf(matrices);
	}
	if (is_skinned)
	{
		computeSkinMatrices(*pose, *this, matrices);
	}

	for (int mesh_index = m_lods[0].from_mesh; mesh_index <= m_lods[0].to_mesh; ++mesh_index)
	{
		Mesh& mesh = m_meshes[mesh_index];
		bool is_mesh_skinned = !mesh.skin.empty();
		u16* indices16 = (u16*)&mesh.indices[0];
		u32* indices32 = (u32*)&mesh.indices[0];
		bool is16 = mesh.flags & (u32)Mesh::Flags::INDICES_16_BIT;
		int index_size = is16 ? 2 : 4;
		for(int i = 0, c = mesh.indices.size() / index_size; i < c; i += 3)
		{
			Vec3 p0, p1, p2;
			if (is16)
			{
				p0 = mesh.vertices[indices16[i]];
				p1 = mesh.vertices[indices16[i + 1]];
				p2 = mesh.vertices[indices16[i + 2]];
				if (is_mesh_skinned)
				{
					p0 = evaluateSkin(p0, mesh.skin[indices16[i]], matrices);
					p1 = evaluateSkin(p1, mesh.skin[indices16[i + 1]], matrices);
					p2 = evaluateSkin(p2, mesh.skin[indices16[i + 2]], matrices);
				}
			}
			else
			{
				p0 = mesh.vertices[indices32[i]];
				p1 = mesh.vertices[indices32[i + 1]];
				p2 = mesh.vertices[indices32[i + 2]];
				if (is_mesh_skinned)
				{
					p0 = evaluateSkin(p0, mesh.skin[indices32[i]], matrices);
					p1 = evaluateSkin(p1, mesh.skin[indices32[i + 1]], matrices);
					p2 = evaluateSkin(p2, mesh.skin[indices32[i + 2]], matrices);
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
		else if (getBoneCount() > 0)
		{
			mesh.type = mesh.skin.empty() ? Mesh::RIGID_INSTANCED : Mesh::SKINNED;
		}
		else mesh.type = Mesh::RIGID_INSTANCED;
	}
}


void Model::setKeepSkin()
{
	if (m_loading_flags & (u32)LoadingFlags::KEEP_SKIN) return;
	m_loading_flags = m_loading_flags | (u32)LoadingFlags::KEEP_SKIN;
	if (isReady()) m_resource_manager.reload(*this);
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


bool Model::parseMeshes(const bgfx::VertexDecl& global_vertex_decl, FS::IFile& file, FileVersion version)
{
	if (version <= FileVersion::MULTIPLE_VERTEX_DECLS) return parseMeshesOld(global_vertex_decl, file, version);
	
	int object_count = 0;
	file.read(&object_count, sizeof(object_count));
	if (object_count <= 0) return false;

	char model_dir[MAX_PATH_LENGTH];
	PathUtils::getDir(model_dir, MAX_PATH_LENGTH, getPath().c_str());

	m_meshes.reserve(object_count);
	for (int i = 0; i < object_count; ++i)
	{
		bgfx::VertexDecl vertex_decl;
		if (!parseVertexDeclEx(file, &vertex_decl)) return false;

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
		
		file.read(&str_size, sizeof(str_size));
		char mesh_name[MAX_PATH_LENGTH];
		mesh_name[str_size] = 0;
		file.read(mesh_name, str_size);

		Mesh& mesh = m_meshes.emplace(material, vertex_decl, mesh_name, m_allocator);
	}

	for (int i = 0; i < object_count; ++i)
	{
		Mesh& mesh = m_meshes[i];
		int index_size;
		int indices_count;
		file.read(&index_size, sizeof(index_size));
		if (index_size != 2 && index_size != 0) return false;
		file.read(&indices_count, sizeof(indices_count));
		if (indices_count <= 0) return false;
		mesh.indices.resize(index_size * indices_count);
		file.read(&mesh.indices[0], mesh.indices.size());

		mesh.flags = index_size == 2 ? Mesh::Flags::INDICES_16_BIT : 0;
		mesh.indices_count = indices_count;
		const bgfx::Memory* indices_mem = bgfx::copy(&mesh.indices[0], mesh.indices.size());
		mesh.index_buffer_handle = bgfx::createIndexBuffer(indices_mem);
	}

	for (int i = 0; i < object_count; ++i)
	{
		Mesh& mesh = m_meshes[i];
		int data_size;
		file.read(&data_size, sizeof(data_size));
		const bgfx::Memory* vertices_mem = bgfx::alloc(data_size);
		file.read(vertices_mem->data, vertices_mem->size);
		mesh.vertex_buffer_handle = bgfx::createVertexBuffer(vertices_mem, mesh.vertex_decl);

		const bgfx::VertexDecl& vertex_decl = mesh.vertex_decl;
		int position_attribute_offset = vertex_decl.getOffset(bgfx::Attrib::Position);
		int uv_attribute_offset = vertex_decl.getOffset(bgfx::Attrib::TexCoord0);
		int weights_attribute_offset = vertex_decl.getOffset(bgfx::Attrib::Weight);
		int bone_indices_attribute_offset = vertex_decl.getOffset(bgfx::Attrib::Indices);
		bool keep_skin = m_loading_flags & (u32)LoadingFlags::KEEP_SKIN;
		keep_skin = keep_skin && vertex_decl.has(bgfx::Attrib::Weight) && vertex_decl.has(bgfx::Attrib::Indices);

		int vertex_size = mesh.vertex_decl.getStride();
		int mesh_vertex_count = vertices_mem->size / mesh.vertex_decl.getStride();
		mesh.vertices.resize(mesh_vertex_count);
		mesh.uvs.resize(mesh_vertex_count);
		if (keep_skin) mesh.skin.resize(mesh_vertex_count);
		const u8* vertices = vertices_mem->data;
		for (int j = 0; j < mesh_vertex_count; ++j)
		{
			int offset = j * vertex_size;
			if (keep_skin)
			{
				mesh.skin[j].weights = *(const Vec4*)&vertices[offset + weights_attribute_offset];
				copyMemory(mesh.skin[j].indices,
					&vertices[offset + bone_indices_attribute_offset],
					sizeof(mesh.skin[j].indices));
			}
			mesh.vertices[j] = *(const Vec3*)&vertices[offset + position_attribute_offset];
			mesh.uvs[j] = *(const Vec2*)&vertices[offset + uv_attribute_offset];
		}
	}
	file.read(&m_bounding_radius, sizeof(m_bounding_radius));
	file.read(&m_aabb, sizeof(m_aabb));

	return true;
}


bool Model::parseMeshesOld(bgfx::VertexDecl global_vertex_decl, FS::IFile& file, FileVersion version)
{
	int object_count = 0;
	file.read(&object_count, sizeof(object_count));
	if (object_count <= 0) return false;

	m_meshes.reserve(object_count);
	char model_dir[MAX_PATH_LENGTH];
	PathUtils::getDir(model_dir, MAX_PATH_LENGTH, getPath().c_str());
	struct Offsets
	{
		i32 attribute_array_offset;
		i32 attribute_array_size;
		i32 indices_offset;
		i32 mesh_tri_count;
	};
	Array<Offsets> mesh_offsets(m_allocator);
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

		Offsets& offsets = mesh_offsets.emplace();
		file.read(&offsets.attribute_array_offset, sizeof(offsets.attribute_array_offset));
		file.read(&offsets.attribute_array_size, sizeof(offsets.attribute_array_size));
		file.read(&offsets.indices_offset, sizeof(offsets.indices_offset));
		file.read(&offsets.mesh_tri_count, sizeof(offsets.mesh_tri_count));

		file.read(&str_size, sizeof(str_size));
		if (str_size >= MAX_PATH_LENGTH)
		{
			material_manager->unload(*material);
			return false;
		}

		char mesh_name[MAX_PATH_LENGTH];
		mesh_name[str_size] = 0;
		file.read(mesh_name, str_size);

		bgfx::VertexDecl vertex_decl = global_vertex_decl;
		if (version <= FileVersion::SINGLE_VERTEX_DECL)
		{
			parseVertexDecl(file, &vertex_decl);
			if (i != 0 && global_vertex_decl.m_hash != vertex_decl.m_hash)
			{
				g_log_error.log("Renderer") << "Model " << getPath().c_str()
					<< " contains meshes with different vertex declarations.";
			}
			if(i == 0) global_vertex_decl = vertex_decl;
		}


		m_meshes.emplace(material,
			vertex_decl,
			mesh_name,
			m_allocator);
		addDependency(*material);
	}

	i32 indices_count = 0;
	file.read(&indices_count, sizeof(indices_count));
	if (indices_count <= 0) return false;

	int index_size = (m_flags & (u32)Model::Flags::INDICES_16BIT) ? 2 : 4;
	Array<u8> indices(m_allocator);
	indices.resize(indices_count * index_size);
	file.read(&indices[0], indices.size());

	i32 vertices_size = 0;
	file.read(&vertices_size, sizeof(vertices_size));
	if (vertices_size <= 0) return false;

	Array<u8> vertices(m_allocator);
	vertices.resize(vertices_size);
	file.read(&vertices[0], vertices.size());

	int vertex_count = 0;
	for (const Offsets& offsets : mesh_offsets)
	{
		vertex_count += offsets.attribute_array_size / global_vertex_decl.getStride();
	}

	if (version > FileVersion::BOUNDING_SHAPES_PRECOMPUTED)
	{
		file.read(&m_bounding_radius, sizeof(m_bounding_radius));
		file.read(&m_aabb, sizeof(m_aabb));
	}

	float bounding_radius_squared = 0;
	Vec3 min_vertex(0, 0, 0);
	Vec3 max_vertex(0, 0, 0);

	int vertex_size = global_vertex_decl.getStride();
	int position_attribute_offset = global_vertex_decl.getOffset(bgfx::Attrib::Position);
	int uv_attribute_offset = global_vertex_decl.getOffset(bgfx::Attrib::TexCoord0);
	int weights_attribute_offset = global_vertex_decl.getOffset(bgfx::Attrib::Weight);
	int bone_indices_attribute_offset = global_vertex_decl.getOffset(bgfx::Attrib::Indices);
	bool keep_skin = m_loading_flags & (u32)LoadingFlags::KEEP_SKIN;
	keep_skin = keep_skin && global_vertex_decl.has(bgfx::Attrib::Weight) && global_vertex_decl.has(bgfx::Attrib::Indices);
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		Offsets& offsets = mesh_offsets[i];
		Mesh& mesh = m_meshes[i];
		mesh.indices_count = offsets.mesh_tri_count * 3;
		mesh.indices.resize(mesh.indices_count * index_size);
		copyMemory(&mesh.indices[0], &indices[offsets.indices_offset * index_size], mesh.indices_count * index_size);

		int mesh_vertex_count = offsets.attribute_array_size / global_vertex_decl.getStride();
		int mesh_attributes_array_offset = offsets.attribute_array_offset;
		mesh.vertices.resize(mesh_vertex_count);
		mesh.uvs.resize(mesh_vertex_count);
		if (keep_skin) mesh.skin.resize(mesh_vertex_count);
		for (int j = 0; j < mesh_vertex_count; ++j)
		{
			int offset = mesh_attributes_array_offset + j * vertex_size;
			if (keep_skin)
			{
				mesh.skin[j].weights = *(const Vec4*)&vertices[offset + weights_attribute_offset];
				copyMemory(mesh.skin[j].indices,
					&vertices[offset + bone_indices_attribute_offset],
					sizeof(mesh.skin[j].indices));
			}
			mesh.vertices[j] = *(const Vec3*)&vertices[offset + position_attribute_offset];
			mesh.uvs[j] = *(const Vec2*)&vertices[offset + uv_attribute_offset];
			float sq_len = mesh.vertices[j].squaredLength();
			bounding_radius_squared = Math::maximum(bounding_radius_squared, sq_len > 0 ? sq_len : 0);
			min_vertex.x = Math::minimum(min_vertex.x, mesh.vertices[j].x);
			min_vertex.y = Math::minimum(min_vertex.y, mesh.vertices[j].y);
			min_vertex.z = Math::minimum(min_vertex.z, mesh.vertices[j].z);
			max_vertex.x = Math::maximum(max_vertex.x, mesh.vertices[j].x);
			max_vertex.y = Math::maximum(max_vertex.y, mesh.vertices[j].y);
			max_vertex.z = Math::maximum(max_vertex.z, mesh.vertices[j].z);
		}
	}

	if (version <= FileVersion::BOUNDING_SHAPES_PRECOMPUTED)
	{
		m_bounding_radius = sqrt(bounding_radius_squared);
		m_aabb = AABB(min_vertex, max_vertex);
	}

	for (int i = 0; i < m_meshes.size(); ++i)
	{
		Mesh& mesh = m_meshes[i];
		Offsets offsets = mesh_offsets[i];
		
		ASSERT(!bgfx::isValid(mesh.index_buffer_handle));
		mesh.flags = (m_flags & (u32)Flags::INDICES_16BIT) != 0 ? Mesh::Flags::INDICES_16_BIT : 0;
		int indices_size = index_size * mesh.indices_count;
		const bgfx::Memory* mem = bgfx::copy(&indices[offsets.indices_offset * index_size], indices_size);
		mesh.index_buffer_handle = bgfx::createIndexBuffer(mem, index_size == 4 ? BGFX_BUFFER_INDEX32 : 0);
		if (!bgfx::isValid(mesh.index_buffer_handle)) return false;

		ASSERT(!bgfx::isValid(mesh.vertex_buffer_handle));
		const bgfx::Memory* vertices_mem = bgfx::copy(&vertices[offsets.attribute_array_offset], offsets.attribute_array_size);
		mesh.vertex_buffer_handle = bgfx::createVertexBuffer(vertices_mem, mesh.vertex_decl);
		if (!bgfx::isValid(mesh.vertex_buffer_handle)) return false;
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

	bgfx::VertexDecl global_vertex_decl;
	if (header.version > (u32)FileVersion::SINGLE_VERTEX_DECL && header.version <= (u32)FileVersion::MULTIPLE_VERTEX_DECLS)
	{
		parseVertexDeclEx(file, &global_vertex_decl);
	}

	if (parseMeshes(global_vertex_decl, file, (FileVersion)header.version)
		&& parseBones(file)
		&& parseLODs(file))
	{
		m_size = file.size();
		return true;
	}

	g_log_error.log("Renderer") << "Error loading model " << getPath().c_str();
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
	for (Mesh& mesh : m_meshes)
	{
		if (bgfx::isValid(mesh.index_buffer_handle)) bgfx::destroy(mesh.index_buffer_handle);
		if (bgfx::isValid(mesh.vertex_buffer_handle)) bgfx::destroy(mesh.vertex_buffer_handle);
	}
	m_meshes.clear();
	m_bones.clear();
}


} // ~namespace Lumix
