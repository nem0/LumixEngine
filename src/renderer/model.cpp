#include "engine/lumix.h"

#include "core/array.h"
#include "core/crt.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/math.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/stream.h"
#include "engine/component_types.h"
#include "engine/file_system.h"
#include "engine/resource_manager.h"
#include "renderer/draw_stream.h"
#include "renderer/material.h"
#include "renderer/mesh_clusters.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/renderer.h"


namespace Lumix
{

static LocalRigidTransform invert(const LocalRigidTransform& tr)
{
	LocalRigidTransform result;
	result.rot = tr.rot.conjugated();
	result.pos = result.rot.rotate(-tr.pos);
	return result;
}

Mesh::Mesh(const VertexDecl& vertex_decl,
	u8 vb_stride,
	StringView name,
	const AttributeSemantic* semantics,
	Renderer& renderer,
	IAllocator& allocator)
	: name(name, allocator)
	, indices(allocator)
	, vertices(allocator)
	, skin(allocator)
	, vertex_decl(vertex_decl)
	, renderer(renderer)
	, vb_stride(vb_stride)
	, vertex_buffer_handle(gpu::INVALID_BUFFER)
	, index_buffer_handle(gpu::INVALID_BUFFER)
	, index_type(gpu::DataType::U32)
{
	for(AttributeSemantic& attr : attributes_semantic) {
		attr = AttributeSemantic::NONE;
	}
	if (semantics) {
		for(u32 i = 0; i < vertex_decl.attributes_count; ++i) {
			attributes_semantic[i] = semantics[i];
		}
	}
	
	semantics_defines = renderer.getSemanticDefines(Span(attributes_semantic), vertex_decl, vb_stride);
}

Mesh::Mesh(Mesh&& rhs)
	: type(rhs.type)
	, indices(rhs.indices)
	, vertices(rhs.vertices.move())
	, skin(rhs.skin.move())
	, flags(rhs.flags)
	, name(rhs.name)
	, vertex_decl(rhs.vertex_decl)
	, lod(rhs.lod)
	, renderer(rhs.renderer)
{
	ASSERT(false); // renderer keeps Mesh* pointer, so we should not move
}

static bool hasAttribute(Mesh& mesh, AttributeSemantic attribute)
{
	for(const AttributeSemantic& attr : mesh.attributes_semantic) {
		if(attr == attribute) return true;
	}
	return false;
}


const ResourceType Model::TYPE("model");


Model::Model(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator, m_path.c_str())
	, m_bone_map(m_allocator)
	, m_meshes(m_allocator)
	, m_mesh_material(m_allocator)
	, m_bones(m_allocator)
	, m_first_nonroot_bone_index(0)
	, m_renderer(renderer)
	, m_parents(m_allocator)
{
	for (LODMeshIndices& i : m_lod_indices) i = {0, -1};
	for (float & i : m_lod_distances) i = FLT_MAX;
}


static Vec3 evaluateSkin(Vec3& p, Mesh::Skin s, const Matrix* matrices)
{
	Matrix m = matrices[s.indices[0]] * s.weights.x + matrices[s.indices[1]] * s.weights.y +
			   matrices[s.indices[2]] * s.weights.z + matrices[s.indices[3]] * s.weights.w;

	return m.transformPoint(p);
}

Vec3 Model::evalVertexPose(const Pose& pose, u32 mesh_idx, u32 index) const {
	const Mesh& mesh = m_meshes[mesh_idx];
	const Mesh::Skin& skin = mesh.skin[index];

	Matrix matrices[4];
	for (u32 i = 0; i < 4; ++i) {
		const i16 bone_idx = skin.indices[i];
		const Bone& bone = m_bones[bone_idx];
		LocalRigidTransform tmp = { pose.positions[bone_idx], pose.rotations[bone_idx] };
		matrices[i] = (tmp * getInverseBindTransform(bone_idx)).toMatrix();
	}

	const Matrix m = matrices[0] * skin.weights.x
		+ matrices[1] * skin.weights.y
		+ matrices[2] * skin.weights.z
		+ matrices[3] * skin.weights.w;

	return m.transformPoint(mesh.vertices[index]);
}


static void computeSkinMatrices(const Pose& pose, const Model& model, Matrix* matrices) {
	for (u32 i = 0; i < pose.count; ++i) {
		LocalRigidTransform tmp = { pose.positions[i], pose.rotations[i] };
		matrices[i] = (tmp * model.getInverseBindTransform(i)).toMatrix();
	}
}

RayCastModelHit Model::castRay(const Vec3& origin, const Vec3& dir, const Pose* pose, EntityPtr entity, const RayCastModelHit::Filter* filter) {
	RayCastModelHit hit;
	hit.is_hit = false;
	if (!isReady()) return hit;

	Matrix matrices[256];
	ASSERT(!pose || pose->count <= lengthOf(matrices));
	bool is_skinned = false;
	for (int mesh_index = m_lod_indices[0].from; mesh_index <= m_lod_indices[0].to; ++mesh_index) {
		Mesh& mesh = m_meshes[mesh_index];
		is_skinned = pose && !mesh.skin.empty() && pose->count <= lengthOf(matrices);
	}
	if (is_skinned) {
		computeSkinMatrices(*pose, *this, matrices);
	}

	for (int mesh_index = m_lod_indices[0].from; mesh_index <= m_lod_indices[0].to; ++mesh_index) {
		const Mesh& mesh = m_meshes[mesh_index];
		const bool is_mesh_skinned = !mesh.skin.empty() && is_skinned;
		const u16* indices16 = (const u16*)mesh.indices.data();
		const u32* indices32 = (const u32*)mesh.indices.data();
		const bool is16 = mesh.flags & Mesh::Flags::INDICES_16_BIT;
		const int index_size = is16 ? 2 : 4;
		const Vec3* vertices = mesh.vertices.begin();
		
		for (i32 i = 0, c = (i32)mesh.indices.size() / index_size; i < c; i += 3) {
			Vec3 p0, p1, p2;
			if (is16) {
				p0 = vertices[indices16[i]];
				p1 = vertices[indices16[i + 1]];
				p2 = vertices[indices16[i + 2]];
				if (is_mesh_skinned) {
					p0 = evaluateSkin(p0, mesh.skin[indices16[i]], matrices);
					p1 = evaluateSkin(p1, mesh.skin[indices16[i + 1]], matrices);
					p2 = evaluateSkin(p2, mesh.skin[indices16[i + 2]], matrices);
				}
			} else {
				p0 = vertices[indices32[i]];
				p1 = vertices[indices32[i + 1]];
				p2 = vertices[indices32[i + 2]];
				if (is_mesh_skinned) {
					p0 = evaluateSkin(p0, mesh.skin[indices32[i]], matrices);
					p1 = evaluateSkin(p1, mesh.skin[indices32[i + 1]], matrices);
					p2 = evaluateSkin(p2, mesh.skin[indices32[i + 2]], matrices);
				}
			}

			Vec3 normal = cross(p1 - p0, p2 - p0);
			float q = dot(normal, dir);
			if (q == 0)	continue;

			float d = -dot(normal, p0);
			float t = -(dot(normal, origin) + d) / q;
			if (t < 0) continue;

			Vec3 hit_point = origin + dir * t;

			Vec3 edge0 = p1 - p0;
			Vec3 VP0 = hit_point - p0;
			if (dot(normal, cross(edge0, VP0)) < 0) continue;

			Vec3 edge1 = p2 - p1;
			Vec3 VP1 = hit_point - p1;
			if (dot(normal, cross(edge1, VP1)) < 0) continue;

			Vec3 edge2 = p0 - p2;
			Vec3 VP2 = hit_point - p2;
			if (dot(normal, cross(edge2, VP2)) < 0) continue;

			if (!hit.is_hit || hit.t > t)
			{
				RayCastModelHit prev = hit;
				hit.is_hit = true;
				hit.t = t;
				hit.entity = entity;
				hit.mesh = &m_meshes[mesh_index];
				hit.component_type = types::model_instance;
				if (filter && !filter->invoke(hit)) hit = prev;
			}
		}
	}
	hit.origin = DVec3(origin.x, origin.y, origin.z);
	hit.dir = dir;
	return hit;
}


void Model::getRelativePose(Pose& pose)
{
	ASSERT(pose.count == m_bones.size());
	Vec3* pos = pose.positions;
	Quat* rot = pose.rotations;
	for (int i = 0, c = m_bones.size(); i < c; ++i)
	{
		pos[i] = m_bones[i].relative_transform.pos;
		rot[i] = m_bones[i].relative_transform.rot;
	}
	pose.is_absolute = false;
}


void Model::getPose(Pose& pose)
{
	ASSERT(pose.count == m_bones.size());
	Vec3* pos = pose.positions;
	Quat* rot = pose.rotations;
	for (int i = 0, c = m_bones.size(); i < c; ++i)
	{
		pos[i] = m_bones[i].transform.pos;
		rot[i] = m_bones[i].transform.rot;
	}
	pose.is_absolute = true;
}


static bool parseVertexDecl(IInputStream& file, VertexDecl* vertex_decl, AttributeSemantic* semantics, u32& vb_stride)
{
	u32 attribute_count;
	file.read(&attribute_count, sizeof(attribute_count));
	vertex_decl->attributes_count = 0;

	u8 offset = 0;
	for (u32 i = 0; i < attribute_count; ++i) {
		AttributeType type;
		u8 cmp_count;
		file.read(semantics[i]);
		file.read(type);
		file.read(cmp_count);

		switch(semantics[i]) {
			case AttributeSemantic::WEIGHTS:
				if (type != AttributeType::FLOAT) {
					vertex_decl->addAttribute(offset, cmp_count, type, VertexAttribute::NORMALIZED);
				}
				else {
					vertex_decl->addAttribute(offset, cmp_count, type, 0);
				}
				break;
			case AttributeSemantic::POSITION:
			case AttributeSemantic::TEXCOORD0:
				vertex_decl->addAttribute(offset, cmp_count, type, 0);
				break;
			case AttributeSemantic::AO:
			case AttributeSemantic::COLOR0:
				vertex_decl->addAttribute(offset, cmp_count, type, VertexAttribute::NORMALIZED);
				break;
			case AttributeSemantic::NORMAL:
			case AttributeSemantic::TANGENT:
				if (type == AttributeType::FLOAT) {
					vertex_decl->addAttribute(offset, cmp_count, type, 0);
				}
				else {
					vertex_decl->addAttribute(offset, cmp_count, type, VertexAttribute::NORMALIZED);
				}
				break;
			case AttributeSemantic::JOINTS:
				vertex_decl->addAttribute(offset, cmp_count, type, 0);
				break;
			default: ASSERT(false); break;
		}

		offset += getSize(type) * cmp_count;
	}
	vb_stride = offset;

	return true;
}


void Model::onBeforeReady()
{
	for (u32 i = 0, n = m_meshes.size(); i < n; ++i) {
		Mesh& mesh = m_meshes[i];
		mesh.type = m_bones.empty() || mesh.skin.empty() ? Mesh::RIGID : Mesh::SKINNED;
	}

	for (u32 i = 0; i < 4; ++i) {
		if (m_lod_indices[i].from < 0) continue;

		for (i32 j = m_lod_indices[i].from; j <= m_lod_indices[i].to; ++j) {
			m_meshes[j].lod = float(i);
		}
	}

	for (u32 i = 0, n = m_meshes.size(); i < n; ++i) {
		Mesh& mesh = m_meshes[i];
		MeshMaterial& mesh_mat = m_mesh_material[i];
		
		RollingHasher hasher;
		const Material* material = mesh_mat.material;
		const Shader* shader = material->getShader();
		const u32 define_mask = material->getDefineMask();
		hasher.begin();
		Mesh* mesh_ptr = &mesh;
		hasher.update(&mesh_ptr, sizeof(mesh_ptr));
		hasher.update(&shader, sizeof(shader));
		hasher.update(&define_mask, sizeof(define_mask));
		hasher.update(&material->m_render_states, sizeof(material->m_render_states));
		const RuntimeHash32 hash = hasher.end();
		mesh_mat.sort_key = m_renderer.allocSortKey(hash.getHashValue());
		mesh_mat.material_index = mesh_mat.material->getIndex();
	}
}


bool Model::parseBones(InputMemoryStream& file)
{
	int bone_count;
	file.read(bone_count);
	if (bone_count < 0) return false;
	if (bone_count > Bone::MAX_COUNT) {
		logWarning("Model ", getPath(), " has too many bones.");
		return false;
	}

	m_bones.reserve(bone_count);
	for (int i = 0; i < bone_count; ++i) {
		Model::Bone& b = m_bones.emplace(m_allocator);
		u32 len;
		file.read(len);
		const void* name = file.skip(len);
		b.name = StringView((const char*)name, (const char*)name + len);
		m_bone_map.insert(BoneNameHash(b.name.c_str()), m_bones.size() - 1);
		i32 parent_idx;
		file.read(parent_idx);
		m_parents.emplace(parent_idx);
		file.read(b.transform.pos);
		file.read(b.transform.rot);
	}

	m_first_nonroot_bone_index = -1;
	for (int i = 0; i < bone_count; ++i) {
		Model::Bone& b = m_bones[i];
		i32 parent_idx = m_parents[i];
		if (parent_idx < 0) {
			if (m_first_nonroot_bone_index != -1) {
				logError("Invalid skeleton in ", getPath());
				return false;
			}
			m_parents[i] = -1;
		}
		else {
			if (parent_idx > i) {
				logError("Invalid skeleton in ", getPath());
				return false;
			}
			if (m_first_nonroot_bone_index == -1) m_first_nonroot_bone_index = i;
		}
	}

	u32 num_soa = ((m_bones.size() + 3) / 4) * 4;
	float* soa = (float*)m_allocator.allocate(sizeof(LocalRigidTransform) * num_soa, 16);
	m_inverse_bind.px = soa; soa += num_soa;
	m_inverse_bind.py = soa; soa += num_soa;
	m_inverse_bind.pz = soa; soa += num_soa;
	m_inverse_bind.rx = soa; soa += num_soa;
	m_inverse_bind.ry = soa; soa += num_soa;
	m_inverse_bind.rz = soa; soa += num_soa;
	m_inverse_bind.rw = soa; soa += num_soa;
	for (i32 i = 0, c = m_bones.size(); i < c; ++i) {
		LocalRigidTransform inv_bind = invert(m_bones[i].transform);
		m_inverse_bind.px[i] = inv_bind.pos.x;
		m_inverse_bind.py[i] = inv_bind.pos.y;
		m_inverse_bind.pz[i] = inv_bind.pos.z;
		m_inverse_bind.rx[i] = inv_bind.rot.x;
		m_inverse_bind.ry[i] = inv_bind.rot.y;
		m_inverse_bind.rz[i] = inv_bind.rot.z;
		m_inverse_bind.rw[i] = inv_bind.rot.w;
	}

	for (int i = 0; i < m_bones.size(); ++i) {
		int p = m_parents[i];
		if (p >= 0) {
			m_bones[i].relative_transform = getInverseBindTransform(p) * m_bones[i].transform;
		}
		else
		{
			m_bones[i].relative_transform = m_bones[i].transform;
		}
	}

	return true;
}

LocalRigidTransform Model::getInverseBindTransform(i32 bone_idx) const {
	LocalRigidTransform res;
	res.pos.x = m_inverse_bind.px[bone_idx];
	res.pos.y = m_inverse_bind.py[bone_idx];
	res.pos.z = m_inverse_bind.pz[bone_idx];
	res.rot.x = m_inverse_bind.rx[bone_idx];
	res.rot.y = m_inverse_bind.ry[bone_idx];
	res.rot.z = m_inverse_bind.rz[bone_idx];
	res.rot.w = m_inverse_bind.rw[bone_idx];
	return res;
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


static int getAttributeOffset(Mesh& mesh, AttributeSemantic attr)
{
	for (u32 i = 0; i < lengthOf(mesh.attributes_semantic); ++i) {
		if(mesh.attributes_semantic[i] == attr) {
			return mesh.vertex_decl.attributes[i].byte_offset;
		}
	}
	return -1;
}


bool Model::parseMeshes(InputMemoryStream& file, FileVersion version)
{
	int object_count = 0;
	file.read(object_count);

	ASSERT(m_meshes.empty());
	m_meshes.reserve(object_count);
	for (int i = 0; i < object_count; ++i)
	{
		VertexDecl vertex_decl;
		AttributeSemantic semantics[VertexDecl::MAX_ATTRIBUTES];
		for(auto& sem : semantics) sem = AttributeSemantic::NONE;
		u32 vb_stride;
		if (!parseVertexDecl(file, &vertex_decl, semantics, vb_stride)) return false;

		u32 mat_path_length;
		file.read(mat_path_length);
		const void* mat_path_v = file.skip(mat_path_length);
		StringView mat_path((const char*)mat_path_v, mat_path_length);
		
		Material* material = m_resource_manager.getOwner().load<Material>(Path(mat_path));
	
		u32 str_size;
		file.read(str_size);
		const void* tmp = file.skip(str_size);

		StringView mesh_name((const char*)tmp, str_size);

		if (version >= FileVersion::STRIDE) {
			// vertices are padded to 4 bytes, so that vertex pulling can use wide aligned loads
			u32 stride;
			file.read(stride);
			if (stride < vb_stride || stride > 255) {
				logError(m_path, ": invalid vertex stride");
				return false;
			}
			vb_stride = stride;
		}

		m_meshes.emplace(vertex_decl, vb_stride, mesh_name, semantics, m_renderer, m_allocator);
		m_mesh_material.emplace(material);
		addDependency(*material);
	}

	for (int i = 0; i < object_count; ++i)
	{
		Mesh& mesh = m_meshes[i];
		int index_size;
		int indices_count;
		file.read(index_size);
		if (index_size != 2 && index_size != 4) {
			logError(m_path, ": invalid index size");
			return false;
		}
		file.read(indices_count);
		if (indices_count < 0 || (indices_count == 0 && version < FileVersion::CLUSTERS)) {
			logError(m_path, ": has no geometry data");
			return false;
		}
		if (indices_count == 0) {
			// indices of the first LOD are stored as the finest level of clusters, see parseClusters
			mesh.indices_count = 0;
			mesh.index_type = gpu::DataType::U32;
			continue;
		}
		mesh.indices.resize(index_size * indices_count);
		mesh.indices_count = indices_count;
		file.read(mesh.indices.getMutableData(), mesh.indices.size());

		if (index_size == 2) mesh.flags |= Mesh::Flags::INDICES_16_BIT;
		const Renderer::MemRef mem = m_renderer.copy(mesh.indices.data(), (u32)mesh.indices.size());
		mesh.index_buffer_handle = m_renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE, m_path.c_str());
		mesh.index_type = index_size == 2 ? gpu::DataType::U16 : gpu::DataType::U32;
		if (!mesh.index_buffer_handle) {
			logError(m_path, ": failed to create index buffer");
			return false;
		}
	}

	for (int i = 0; i < object_count; ++i)
	{
		Mesh& mesh = m_meshes[i];
		int data_size;
		file.read(data_size);
		if (data_size == 0) {
			// vertices of meshes with clusters are in the clusters, see parseClusters (it fails if a mesh has no vertices)
			continue;
		}
		Renderer::MemRef vertices_mem = m_renderer.allocate(data_size);
		file.read(vertices_mem.data, data_size);

		int position_attribute_offset = getAttributeOffset(mesh, AttributeSemantic::POSITION);
		int weights_attribute_offset = getAttributeOffset(mesh, AttributeSemantic::WEIGHTS);
		int bone_indices_attribute_offset = getAttributeOffset(mesh, AttributeSemantic::JOINTS);
		bool keep_skin = hasAttribute(mesh, AttributeSemantic::WEIGHTS) && hasAttribute(mesh, AttributeSemantic::JOINTS);

		int vertex_size = mesh.vb_stride;
		int mesh_vertex_count = data_size / vertex_size;
		mesh.vertices.resize(mesh_vertex_count);
		if (keep_skin) mesh.skin.resize(mesh_vertex_count);
		const u8* vertices = (const u8*)vertices_mem.data;
		for (int j = 0; j < mesh_vertex_count; ++j) {
			int offset = j * vertex_size;
			if (keep_skin) {
				u16 weights[4];
				memcpy(weights, &vertices[offset + weights_attribute_offset], sizeof(weights));
				mesh.skin[j].weights.x = weights[0] / 65535.f;
				mesh.skin[j].weights.y = weights[1] / 65535.f;
				mesh.skin[j].weights.z = weights[2] / 65535.f;
				mesh.skin[j].weights.w = weights[3] / 65535.f;
				memcpy(mesh.skin[j].indices,
					&vertices[offset + bone_indices_attribute_offset],
					sizeof(mesh.skin[j].indices));
			}
			mesh.vertices[j] = *(const Vec3*)&vertices[offset + position_attribute_offset];
		}
		mesh.vertex_buffer_handle = m_renderer.createBuffer(vertices_mem, gpu::BufferFlags::IMMUTABLE, m_path.c_str());
		if (!mesh.vertex_buffer_handle) {
			logError(m_path, ": failed to create vertex buffer");
			return false;
		}
	}
	file.read(m_origin_bounding_radius);
	file.read(m_center_bounding_radius);
	file.read(m_aabb);

	return true;
}


bool Model::parseLODs(InputMemoryStream& file)
{
	u32 lod_count;
	file.read(lod_count);
	if (lod_count > lengthOf(m_lod_indices)) return false;

	for (float& d : m_lod_distances) d = -1;
	for (LODMeshIndices& i : m_lod_indices) {
		i.from = 0;
		i.to = -1;
	}

	for (u32 i = 0; i < lod_count; ++i) {
		file.read(m_lod_indices[i].to);
		file.read(m_lod_distances[i]);
		m_lod_indices[i].from = i > 0 ? m_lod_indices[i - 1].to + 1 : 0;
	}
	return true;
}


bool Model::load(Span<const u8> mem)
{
	PROFILE_FUNCTION();
	FileHeader header;
	InputMemoryStream file(mem);
	file.read(header);

	if (header.magic != FileHeader::MAGIC)
	{
		logWarning("Corrupted model ", getPath());
		return false;
	}

	if(header.version > FileVersion::LATEST)
	{
		logWarning("Unsupported version of model ", getPath());
		return false;
	}

	if (header.version > FileVersion::ROOT_MOTION_BONE) {
		file.read(m_root_motion_bone);
	}

	if (parseMeshes(file, (FileVersion)header.version)
		&& parseBones(file)
		&& parseLODs(file)
		&& (header.version < FileVersion::CLUSTERS || parseClusters(file, (FileVersion)header.version)))
	{
		return true;
	}

	return false;
}

// Cluster DAGs of the meshes of the first LOD, baked by the model importer (see ModelImporter::writeClusters):
//   u32 mesh_count - meshes [0, mesh_count) have clusters, 0 if the model has none
//   for each mesh: u32 cluster_count, leaf_cluster_count, node_count, root_count, tree_depth, index_stride, leaf_vertex_count, leaf_index_count,
//                  vertex_data_size, vertex_stride, VertexDecl, AttributeSemantic[VertexDecl::MAX_ATTRIBUTES],
//                  MeshClusterGPU[cluster_count], MeshNodeGPU[node_count], u32 roots[root_count], u16 indices[cluster_count * index_stride],
//                  u8 vertex_data[vertex_data_size], Vec3 vertices[leaf_vertex_count], u32 indices[leaf_index_count]
// Everything is ready to be uploaded. The vertex data are the headers of clusters followed by their interleaved, bit packed vertices (see mesh_clusters.h).
// Meshes with clusters have no vertices and indices of their own (see parseMeshes), the CPU ones (`vertices` and `indices`) are the finest clusters.
bool Model::parseClusters(InputMemoryStream& file, FileVersion version) {
	u32 mesh_count;
	file.read(mesh_count);
	if (mesh_count > (u32)m_meshes.size()) return false;
	if (mesh_count > 0 && version < FileVersion::CLUSTER_INTERLEAVED) {
		logError(m_path, ": clusters of an old version, the model has to be compiled again");
		return false;
	}

	u32 total_clusters = 0;
	for (u32 i = 0; i < mesh_count; ++i) {
		Mesh& mesh = m_meshes[i];
		u32 cluster_count, leaf_index_count, leaf_vertex_count, node_count, root_count, index_stride, vertex_data_size;
		file.read(cluster_count);
		file.read(mesh.leaf_cluster_count);
		file.read(node_count);
		file.read(root_count);
		file.read(mesh.tree_depth);
		file.read(index_stride);
		file.read(leaf_vertex_count);
		file.read(leaf_index_count);
		file.read(vertex_data_size);
		file.read(mesh.vb_stride);
		file.read(mesh.vertex_decl);
		file.read(mesh.attributes_semantic);

		bool valid_semantics = mesh.vertex_decl.isValid();
		// unused entries of the array are NONE
		for (AttributeSemantic semantic : mesh.attributes_semantic) valid_semantics = valid_semantics && ((u32)semantic < (u32)AttributeSemantic::COUNT || semantic == AttributeSemantic::NONE);
		// a draw has index_stride indices per cluster, from the sequential index buffer of the pipeline (65536 entries)
		if (file.hasOverflow() || !valid_semantics || mesh.leaf_cluster_count > cluster_count || mesh.tree_depth > 64 || index_stride == 0 || index_stride > 65536) {
			logError(m_path, ": invalid clusters");
			return false;
		}

		const u64 clusters_size = u64(cluster_count) * sizeof(MeshClusterGPU);
		const u64 nodes_size = u64(node_count) * sizeof(MeshNodeGPU) + u64(root_count) * sizeof(u32);
		const u64 indices_size = u64(cluster_count) * index_stride * sizeof(u16);
		const u64 vertices_size = u64(leaf_vertex_count) * sizeof(Vec3);
		const u64 cpu_indices_size = u64(leaf_index_count) * sizeof(u32);
		if (file.hasOverflow() || cluster_count == 0 || node_count == 0 || root_count == 0 || vertex_data_size > 0x7fffFFFF
			|| clusters_size + nodes_size + indices_size + vertex_data_size + vertices_size + cpu_indices_size > file.remaining()) return false;
		const void* clusters = file.skip(clusters_size);
		const void* nodes = file.skip(nodes_size);
		const void* indices = file.skip(indices_size);
		const void* vertex_data = file.skip(vertex_data_size);

		mesh.vertices.resize(leaf_vertex_count);
		memcpy(mesh.vertices.begin(), file.skip(vertices_size), vertices_size);
		mesh.indices.resize(cpu_indices_size);
		memcpy(mesh.indices.getMutableData(), file.skip(cpu_indices_size), cpu_indices_size);
		mesh.indices_count = leaf_index_count;
		mesh.index_type = gpu::DataType::U32;

		mesh.cluster_count = cluster_count;
		mesh.node_count = node_count;
		mesh.root_count = root_count;
		mesh.cluster_index_stride = index_stride;
		mesh.semantics_defines = m_renderer.getSemanticDefines(Span(mesh.attributes_semantic), mesh.vertex_decl, mesh.vb_stride);
		const char* path = m_path.c_str();
		mesh.cluster_buffer = m_renderer.createBuffer(m_renderer.copy(clusters, (u32)clusters_size), gpu::BufferFlags::IMMUTABLE, path);
		mesh.node_buffer = m_renderer.createBuffer(m_renderer.copy(nodes, (u32)nodes_size), gpu::BufferFlags::IMMUTABLE, path);
		// vertex shaders read the indices from this buffer as 32 bit words (see pullVertex in vertex_pulling.hlsli), the size is a multiple of 4
		const u32 indices_padded_size = ((u32)indices_size + 3) & ~3u;
		Renderer::MemRef indices_mem = m_renderer.allocate(indices_padded_size);
		memcpy(indices_mem.data, indices, (size_t)indices_size);
		memset((u8*)indices_mem.data + indices_size, 0, indices_padded_size - (size_t)indices_size);
		mesh.cluster_index_buffer = m_renderer.createBuffer(indices_mem, gpu::BufferFlags::IMMUTABLE, path);
		mesh.vertex_buffer_handle = m_renderer.createBuffer(m_renderer.copy(vertex_data, vertex_data_size), gpu::BufferFlags::IMMUTABLE, path);
		if (!mesh.cluster_buffer || !mesh.cluster_index_buffer || !mesh.node_buffer || !mesh.vertex_buffer_handle) {
			logError(m_path, ": failed to create cluster buffers");
			return false;
		}
		total_clusters += cluster_count;
	}

	// Meshes of the first LOD have vertices only in clusters, so all of them must have clusters
	if (mesh_count > 0 && mesh_count != (u32)(getClusterMeshesEnd() + 1)) {
		logError(m_path, ": not all meshes of the first LOD have clusters");
		return false;
	}
	for (const Mesh& mesh : m_meshes) {
		if (mesh.indices_count == 0 || !mesh.vertex_buffer_handle) {
			logError(m_path, ": has no geometry data");
			return false;
		}
	}

	if (mesh_count > 0) {
		m_has_clusters = true;
		logInfo(m_path, ": loaded ", total_clusters, " clusters");
	}
	return true;
}

void Model::destroyClusters() {
	DrawStream& stream = m_renderer.getDrawStream();
	for (Mesh& mesh : m_meshes) {
		if (mesh.cluster_buffer) stream.destroy(mesh.cluster_buffer);
		if (mesh.node_buffer) stream.destroy(mesh.node_buffer);
		if (mesh.cluster_index_buffer) stream.destroy(mesh.cluster_index_buffer);
		mesh.cluster_buffer = gpu::INVALID_BUFFER;
		mesh.cluster_index_buffer = gpu::INVALID_BUFFER;
		mesh.node_buffer = gpu::INVALID_BUFFER;
		mesh.cluster_count = 0;
		mesh.cluster_index_stride = 0;
		mesh.node_count = 0;
		mesh.root_count = 0;
		mesh.tree_depth = 0;
		mesh.leaf_cluster_count = 0;
	}
	m_has_clusters = false;
}


void Model::unload()
{
	for (int i = 0, n = m_mesh_material.size(); i < n; ++i) {
		MeshMaterial& m = m_mesh_material[i];
		removeDependency(*m.material);
		m.material->decRefCount();
		m_renderer.freeSortKey(m.sort_key);
		m.sort_key = 0;
		if (m.flags && MeshMaterial::OWN_MATERIAL_INDEX) {
			m_renderer.destroyMaterialConstants(m.material_index);
		}
		m.flags = MeshMaterial::NONE;
		m.material_index = MaterialIndex{0};
	}

	destroyClusters();
	for (Mesh& mesh : m_meshes) {
		DrawStream& stream = m_renderer.getDrawStream();
		if (mesh.index_buffer_handle) stream.destroy(mesh.index_buffer_handle);
		if (mesh.vertex_buffer_handle) stream.destroy(mesh.vertex_buffer_handle);
		mesh.index_buffer_handle = gpu::INVALID_BUFFER;
		mesh.vertex_buffer_handle = gpu::INVALID_BUFFER;
	}
	m_meshes.clear();
	m_mesh_material.clear();
	m_bones.clear();
	m_allocator.deallocate(m_inverse_bind.px);
	m_inverse_bind = {};
}


} // namespace Lumix
