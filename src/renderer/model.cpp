#include "engine/lumix.h"
#include "renderer/model.h"

#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/math.h"
#include "renderer/material.h"
#include "renderer/pose.h"
#include "renderer/renderer.h"


namespace Lumix
{


u32 Mesh::s_last_sort_key = 0;


static LocalRigidTransform invert(const LocalRigidTransform& tr)
{
	LocalRigidTransform result;
	result.rot = tr.rot.conjugated();
	result.pos = result.rot.rotate(-tr.pos);
	return result;
}


Mesh::Mesh(Material* mat,
	const gpu::VertexDecl& vertex_decl,
	u8 vb_stride,
	const char* name,
	const AttributeSemantic* semantics,
	Renderer& renderer,
	IAllocator& allocator)
	: name(name, allocator)
	, material(mat)
	, indices(allocator)
	, vertices(allocator)
	, skin(allocator)
	, vertex_decl(vertex_decl)
{
	render_data = LUMIX_NEW(renderer.getAllocator(), RenderData);
	render_data->vb_stride = vb_stride;
	render_data->vertex_buffer_handle = gpu::INVALID_BUFFER;
	render_data->index_buffer_handle = gpu::INVALID_BUFFER;
	render_data->index_type = gpu::DataType::U32;
	for(AttributeSemantic& attr : attributes_semantic) {
		attr = AttributeSemantic::NONE;
	}
	if(semantics) {
		for(u32 i = 0; i < vertex_decl.attributes_count; ++i) {
			attributes_semantic[i] = semantics[i];
		}
	}

	sort_key = s_last_sort_key;
	++s_last_sort_key;
}


static bool hasAttribute(Mesh& mesh, Mesh::AttributeSemantic attribute)
{
	for(const Mesh::AttributeSemantic& attr : mesh.attributes_semantic) {
		if(attr == attribute) return true;
	}
	return false;
}


void Mesh::setMaterial(Material* new_material, Model& model, Renderer& renderer)
{
	if (material) material->getResourceManager().unload(*material);
	material = new_material;
	type = model.getBoneCount() == 0 || skin.empty() ? Mesh::RIGID : Mesh::SKINNED;
}


const ResourceType Model::TYPE("model");


Model::Model(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_bounding_radius()
	, m_allocator(allocator)
	, m_bone_map(m_allocator)
	, m_meshes(m_allocator)
	, m_bones(m_allocator)
	, m_first_nonroot_bone_index(0)
	, m_renderer(renderer)
{
	m_lod_indices[0] = { 0, -1 };
	m_lod_indices[1] = { 0, -1 };
	m_lod_indices[2] = { 0, -1 };
	m_lod_indices[3] = { 0, -1 };
	m_lod_distances[0] = FLT_MAX;
	m_lod_distances[1] = FLT_MAX;
	m_lod_distances[2] = FLT_MAX;
	m_lod_distances[3] = FLT_MAX;
}


Model::~Model()
{
	ASSERT(isEmpty());
}


static Vec3 evaluateSkin(Vec3& p, Mesh::Skin s, const Matrix* matrices)
{
	Matrix m = matrices[s.indices[0]] * s.weights.x + matrices[s.indices[1]] * s.weights.y +
			   matrices[s.indices[2]] * s.weights.z + matrices[s.indices[3]] * s.weights.w;

	return m.transformPoint(p);
}


static void computeSkinMatrices(const Pose& pose, const Model& model, Matrix* matrices)
{
	for (u32 i = 0; i < pose.count; ++i)
	{
		auto& bone = model.getBone(i);
		LocalRigidTransform tmp = { pose.positions[i], pose.rotations[i] };
		matrices[i] = (tmp * bone.inv_bind_transform).toMatrix();
	}
}


bool Model::isSkinned() const
{
	ASSERT(isReady());
	for (const Mesh& m : m_meshes) {
		if(m.type == Mesh::SKINNED) return true;
	}
	return false;
}


RayCastModelHit Model::castRay(const Vec3& origin, const Vec3& dir, const Pose* pose)
{
	RayCastModelHit hit;
	hit.is_hit = false;
	if (!isReady()) return hit;

	Matrix matrices[256];
	ASSERT(!pose || pose->count <= lengthOf(matrices));
	bool is_skinned = false;
	for (int mesh_index = m_lod_indices[0].from; mesh_index <= m_lod_indices[0].to; ++mesh_index)
	{
		Mesh& mesh = m_meshes[mesh_index];
		is_skinned = pose && !mesh.skin.empty() && pose->count <= lengthOf(matrices);
	}
	if (is_skinned)
	{
		computeSkinMatrices(*pose, *this, matrices);
	}

	for (int mesh_index = m_lod_indices[0].from; mesh_index <= m_lod_indices[0].to; ++mesh_index)
	{
		Mesh& mesh = m_meshes[mesh_index];
		bool is_mesh_skinned = !mesh.skin.empty() && is_skinned;
		u16* indices16 = (u16*)mesh.indices.getMutableData();
		u32* indices32 = (u32*)mesh.indices.getMutableData();
		bool is16 = mesh.flags.isSet(Mesh::Flags::INDICES_16_BIT);
		int index_size = is16 ? 2 : 4;
		for(i32 i = 0, c = (i32)mesh.indices.size() / index_size; i < c; i += 3)
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
			float q = dotProduct(normal, dir);
			if (q == 0)	continue;

			float d = -dotProduct(normal, p0);
			float t = -(dotProduct(normal, origin) + d) / q;
			if (t < 0) continue;

			Vec3 hit_point = origin + dir * t;

			Vec3 edge0 = p1 - p0;
			Vec3 VP0 = hit_point - p0;
			if (dotProduct(normal, crossProduct(edge0, VP0)) < 0) continue;

			Vec3 edge1 = p2 - p1;
			Vec3 VP1 = hit_point - p1;
			if (dotProduct(normal, crossProduct(edge1, VP1)) < 0) continue;

			Vec3 edge2 = p0 - p2;
			Vec3 VP2 = hit_point - p2;
			if (dotProduct(normal, crossProduct(edge2, VP2)) < 0) continue;

			if (!hit.is_hit || hit.t > t)
			{
				hit.is_hit = true;
				hit.t = t;
				hit.mesh = &m_meshes[mesh_index];
			}
		}
	}
	hit.origin = DVec3(origin.x, origin.y, origin.z);
	hit.dir = dir;
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


static u8 getIndexBySemantic(Mesh::AttributeSemantic semantic) {
	switch (semantic) {
		case Mesh::AttributeSemantic::POSITION: return 0;
		case Mesh::AttributeSemantic::TEXCOORD0: return 1;
		case Mesh::AttributeSemantic::NORMAL: return 2;
		case Mesh::AttributeSemantic::TANGENT: return 3;
		case Mesh::AttributeSemantic::INDICES: return 4;
		case Mesh::AttributeSemantic::WEIGHTS: return 5;
		case Mesh::AttributeSemantic::COLOR0: return 6;
	}
	ASSERT(false);
	return 0;
}


static bool parseVertexDecl(IInputStream& file, gpu::VertexDecl* vertex_decl, Mesh::AttributeSemantic* semantics, Ref<u32> vb_stride)
{
	u32 attribute_count;
	file.read(&attribute_count, sizeof(attribute_count));
	vertex_decl->attributes_count = 0;

	u8 offset = 0;
	bool is_skinned = false;
	for (u32 i = 0; i < attribute_count; ++i) {
		gpu::AttributeType type;
		u8 cmp_count;
		file.read(semantics[i]);
		file.read(type);
		file.read(cmp_count);

		const u8 idx = getIndexBySemantic(semantics[i]);

		switch(semantics[i]) {
			case Mesh::AttributeSemantic::WEIGHTS:
			case Mesh::AttributeSemantic::POSITION:
			case Mesh::AttributeSemantic::TEXCOORD0:
				vertex_decl->addAttribute(idx, offset, cmp_count, type, 0);
				break;
			case Mesh::AttributeSemantic::COLOR0:
				vertex_decl->addAttribute(idx, offset, cmp_count, type, gpu::Attribute::NORMALIZED);
				break;
			case Mesh::AttributeSemantic::NORMAL:
			case Mesh::AttributeSemantic::TANGENT:
				if (type == gpu::AttributeType::FLOAT) {
					vertex_decl->addAttribute(idx, offset, cmp_count, type, 0);
				}
				else {
					vertex_decl->addAttribute(idx, offset, cmp_count, type, gpu::Attribute::NORMALIZED);
				}
				break;
			case Mesh::AttributeSemantic::INDICES:
				is_skinned = true;
				vertex_decl->addAttribute(idx, offset, cmp_count, type, gpu::Attribute::AS_INT);
				break;
			default: ASSERT(false); break;
		}

		offset += gpu::getSize(type) * cmp_count;
	}
	vb_stride = offset;

	if (!is_skinned) {
		vertex_decl->addAttribute(4, 0, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		vertex_decl->addAttribute(5, 16, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
		// TODO this is here because of grass, find a better solution
		//vertex_decl->addAttribute(6, 32, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);
	}

	return true;
}


void Model::onBeforeReady()
{
	for (Mesh& mesh : m_meshes) {
		mesh.type = getBoneCount() == 0 || mesh.skin.empty() ? Mesh::RIGID : Mesh::SKINNED;
		mesh.layer = mesh.material->getLayer();
	}
}


bool Model::parseBones(InputMemoryStream& file)
{
	int bone_count;
	file.read(bone_count);
	if (bone_count < 0) return false;
	if (bone_count > Bone::MAX_COUNT) {
		logWarning("Renderer") << "Model " << getPath().c_str() << " has too many bones.";
		return false;
	}

	m_bones.reserve(bone_count);
	for (int i = 0; i < bone_count; ++i) {
		Model::Bone& b = m_bones.emplace(m_allocator);
		int len;
		file.read(len);
		char tmp[MAX_PATH_LENGTH];
		if (len >= MAX_PATH_LENGTH) {
			return false;
		}
		file.read(tmp, len);
		tmp[len] = 0;
		b.name = tmp;
		m_bone_map.insert(crc32(b.name.c_str()), m_bones.size() - 1);
		file.read(b.parent_idx);
		file.read(b.transform.pos);
		file.read(b.transform.rot);
	}

	m_first_nonroot_bone_index = -1;
	for (int i = 0; i < bone_count; ++i) {
		Model::Bone& b = m_bones[i];
		if (b.parent_idx < 0) {
			if (m_first_nonroot_bone_index != -1) {
				logError("Renderer") << "Invalid skeleton in " << getPath().c_str();
				return false;
			}
			b.parent_idx = -1;
		}
		else {
			if (b.parent_idx > i) {
				logError("Renderer") << "Invalid skeleton in " << getPath().c_str();
				return false;
			}
			if (m_first_nonroot_bone_index == -1) m_first_nonroot_bone_index = i;
		}
	}

	for (int i = 0; i < m_bones.size(); ++i)
	{
			m_bones[i].inv_bind_transform = invert(m_bones[i].transform);
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


static int getAttributeOffset(Mesh& mesh, Mesh::AttributeSemantic attr)
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
	if (object_count <= 0) return false;

	char model_dir[MAX_PATH_LENGTH];
	Path::getDir(Span(model_dir), getPath().c_str());

	m_meshes.reserve(object_count);
	for (int i = 0; i < object_count; ++i)
	{
		gpu::VertexDecl vertex_decl;
		Mesh::AttributeSemantic semantics[gpu::VertexDecl::MAX_ATTRIBUTES];
		for(auto& i : semantics) i = Mesh::AttributeSemantic::NONE;
		u32 vb_stride;
		if (!parseVertexDecl(file, &vertex_decl, semantics, Ref(vb_stride))) return false;

		u32 mat_path_length;
		char mat_path[MAX_PATH_LENGTH + 128];
		file.read(mat_path_length);
		if (mat_path_length + 1 > lengthOf(mat_path)) return false;
		file.read(mat_path, mat_path_length);
		mat_path[mat_path_length] = '\0';
		
		Material* material = m_resource_manager.getOwner().load<Material>(Path(mat_path));
	
		i32 str_size;
		file.read(str_size);
		char mesh_name[MAX_PATH_LENGTH];
		mesh_name[str_size] = 0;
		file.read(mesh_name, str_size);

		m_meshes.emplace(material, vertex_decl, vb_stride, mesh_name, semantics, m_renderer, m_allocator);
		addDependency(*material);
	}

	for (int i = 0; i < object_count; ++i)
	{
		Mesh& mesh = m_meshes[i];
		int index_size;
		int indices_count;
		file.read(index_size);
		if (index_size != 2 && index_size != 4) return false;
		file.read(indices_count);
		if (indices_count <= 0) return false;
		mesh.indices.resize(index_size * indices_count);
		mesh.render_data->indices_count = indices_count;
		file.read(mesh.indices.getMutableData(), mesh.indices.size());

		if (index_size == 2) mesh.flags.set(Mesh::Flags::INDICES_16_BIT);
		const Renderer::MemRef mem = m_renderer.copy(mesh.indices.data(), (u32)mesh.indices.size());
		mesh.render_data->index_buffer_handle = m_renderer.createBuffer(mem, (u32)gpu::BufferFlags::IMMUTABLE);
		mesh.render_data->index_type = index_size == 2 ? gpu::DataType::U16 : gpu::DataType::U32;
		if (!mesh.render_data->index_buffer_handle.isValid()) return false;
	}

	for (int i = 0; i < object_count; ++i)
	{
		Mesh& mesh = m_meshes[i];
		int data_size;
		file.read(data_size);
		Renderer::MemRef vertices_mem = m_renderer.allocate(data_size);
		file.read(vertices_mem.data, data_size);

		int position_attribute_offset = getAttributeOffset(mesh, Mesh::AttributeSemantic::POSITION);
		int weights_attribute_offset = getAttributeOffset(mesh, Mesh::AttributeSemantic::WEIGHTS);
		int bone_indices_attribute_offset = getAttributeOffset(mesh, Mesh::AttributeSemantic::INDICES);
		bool keep_skin = hasAttribute(mesh, Mesh::AttributeSemantic::WEIGHTS) && hasAttribute(mesh, Mesh::AttributeSemantic::INDICES);

		int vertex_size = mesh.render_data->vb_stride;
		int mesh_vertex_count = data_size / vertex_size;
		mesh.vertices.resize(mesh_vertex_count);
		if (keep_skin) mesh.skin.resize(mesh_vertex_count);
		const u8* vertices = (const u8*)vertices_mem.data;
		for (int j = 0; j < mesh_vertex_count; ++j)
		{
			int offset = j * vertex_size;
			if (keep_skin)
			{
				mesh.skin[j].weights = *(const Vec4*)&vertices[offset + weights_attribute_offset];
				memcpy(mesh.skin[j].indices,
					&vertices[offset + bone_indices_attribute_offset],
					sizeof(mesh.skin[j].indices));
			}
			mesh.vertices[j] = *(const Vec3*)&vertices[offset + position_attribute_offset];
		}
		mesh.render_data->vertex_buffer_handle = m_renderer.createBuffer(vertices_mem, (u32)gpu::BufferFlags::IMMUTABLE);
		if (!mesh.render_data->vertex_buffer_handle.isValid()) return false;
	}
	file.read(m_bounding_radius);
	file.read(m_aabb);

	return true;
}


bool Model::parseLODs(InputMemoryStream& file)
{
	u32 lod_count;
	file.read(lod_count);
	if (lod_count <= 0 || lod_count > lengthOf(m_lod_indices))
	{
		return false;
	}
	for (u32 i = 0; i < lod_count; ++i)
	{
		file.read(m_lod_indices[i].to);
		file.read(m_lod_distances[i]);
		m_lod_indices[i].from = i > 0 ? m_lod_indices[i - 1].to + 1 : 0;
	}
	return true;
}


bool Model::load(u64 size, const u8* mem)
{
	PROFILE_FUNCTION();
	FileHeader header;
	InputMemoryStream file(mem, size);
	file.read(header);

	if (header.magic != FILE_MAGIC)
	{
		logWarning("Renderer") << "Corrupted model " << getPath().c_str();
		return false;
	}

	if(header.version > (u32)FileVersion::LATEST)
	{
		logWarning("Renderer") << "Unsupported version of model " << getPath().c_str();
		return false;
	}

	if (parseMeshes(file, (FileVersion)header.version)
		&& parseBones(file)
		&& parseLODs(file))
	{
		m_size = file.size();
		return true;
	}

	logError("Renderer") << "Error loading model " << getPath().c_str();
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

static const char* getBoneName(Model* model, int bone_index)
{
	return model->getBone(bone_index).name.c_str();
}


void Model::registerLuaAPI(lua_State* L)
{
	#define REGISTER_FUNCTION(F)\
		do { \
			auto f = &LuaWrapper::wrapMethod<&Model::F>; \
			LuaWrapper::createSystemFunction(L, "Model", #F, f); \
		} while(false) \

	REGISTER_FUNCTION(getBoneCount);

	#undef REGISTER_FUNCTION
	

	#define REGISTER_FUNCTION(F)\
		do { \
			auto f = &LuaWrapper::wrap<&F>; \
			LuaWrapper::createSystemFunction(L, "Model", #F, f); \
		} while(false) \

	REGISTER_FUNCTION(getBonePosition);
	REGISTER_FUNCTION(getBoneParent);
	REGISTER_FUNCTION(getBoneName);

	#undef REGISTER_FUNCTION
}


void Model::unload()
{
	auto* material_manager = m_resource_manager.getOwner().get(Material::TYPE);
	for (int i = 0; i < m_meshes.size(); ++i) {
		removeDependency(*m_meshes[i].material);
		material_manager->unload(*m_meshes[i].material);
	}

	for (Mesh& mesh : m_meshes) {
		m_renderer.runInRenderThread(mesh.render_data, [](Renderer& renderer, void* ptr){
			Mesh::RenderData* rd = (Mesh::RenderData*)ptr;
			if (rd->index_buffer_handle.isValid()) gpu::destroy(rd->index_buffer_handle);
			if (rd->vertex_buffer_handle.isValid()) gpu::destroy(rd->vertex_buffer_handle);
			LUMIX_DELETE(renderer.getAllocator(), rd); 
		});
	}
	m_meshes.clear();
	m_bones.clear();
}


} // namespace Lumix
