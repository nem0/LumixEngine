#include "fbx_importer.h"
#include "animation/animation.h"
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/atomic.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/serializer.h"
#include "mikktspace.h"
#include "physics/physics_geometry.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"


namespace Lumix
{


using PathBuilder = StaticString<MAX_PATH_LENGTH>;



static bool hasTangents(const ofbx::Mesh& mesh) {
	if (mesh.getGeometry()->getTangents()) return true;
	if (mesh.getGeometry()->getNormals() && mesh.getGeometry()->getUVs()) return true;
	return false;
}


static void getMaterialName(const ofbx::Material* material, char (&out)[128])
{
	copyString(out, material ? material->name : "default");
	char* iter = out;
	while (*iter)
	{
		char c = *iter;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
		{
			*iter = '_';
		}
		++iter;
	}
	makeLowercase(Span(out), out);
}


void FBXImporter::getImportMeshName(const ImportMesh& mesh, char (&out)[256])
{
	const char* name = mesh.fbx->name;
	const ofbx::Material* material = mesh.fbx_mat;

	if (name[0] == '\0' && mesh.fbx->getParent()) name = mesh.fbx->getParent()->name;
	if (name[0] == '\0' && material) name = material->name;
	copyString(out, name);
	if(mesh.submesh >= 0) {
		catString(out, "_");
		char tmp[32];
		toCString(mesh.submesh, Span(tmp));
		catString(out, tmp);
	}
}


const FBXImporter::ImportMesh* FBXImporter::getAnyMeshFromBone(const ofbx::Object* node, int bone_idx) const
{
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		const ofbx::Mesh* mesh = m_meshes[i].fbx;
		if (m_meshes[i].bone_idx == bone_idx) {
			return &m_meshes[i];
		}

		auto* skin = mesh->getGeometry()->getSkin();
		if (!skin) continue;

		for (int j = 0, c = skin->getClusterCount(); j < c; ++j)
		{
			if (skin->getCluster(j)->getLink() == node) return &m_meshes[i];
		}
	}
	return nullptr;
}


static ofbx::Matrix makeOFBXIdentity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }


static ofbx::Matrix getBindPoseMatrix(const FBXImporter::ImportMesh* mesh, const ofbx::Object* node)
{
	if (!mesh) return node->getGlobalTransform();
	if (!mesh->fbx) return makeOFBXIdentity();

	auto* skin = mesh->fbx->getGeometry()->getSkin();
	if (!skin) return node->getGlobalTransform();

	for (int i = 0, c = skin->getClusterCount(); i < c; ++i)
	{
		const ofbx::Cluster* cluster = skin->getCluster(i);
		if (cluster->getLink() == node)
		{
			return cluster->getTransformLinkMatrix();
		}
	}
	return node->getGlobalTransform();
}


static void extractEmbedded(const ofbx::IScene& scene, const char* src_dir)
{
	for (int i = 0, c = scene.getEmbeddedDataCount(); i < c; ++i) {
		const ofbx::DataView embedded = scene.getEmbeddedData(i);

		ofbx::DataView filename = scene.getEmbeddedFilename(i);
		char path[MAX_PATH_LENGTH];
		filename.toString(path);
		const PathInfo pi(path);
		const StaticString<MAX_PATH_LENGTH> fullpath(src_dir, pi.m_basename, ".", pi.m_extension);

		if (OS::fileExists(fullpath)) return;

		OS::OutputFile file;
		if (!file.open(fullpath)) {
			logError("Renderer") << "Failed to save " << fullpath;
			return;
		}

		if (!file.write(embedded.begin + 4, embedded.end - embedded.begin - 4)) {
			logError("Renderer") << "Failed to write " << fullpath;
		}
		file.close();
	}
}


void FBXImporter::gatherMaterials(const char* src_dir)
{
	for (ImportMesh& mesh : m_meshes)
	{
		const ofbx::Material* fbx_mat = mesh.fbx_mat;
		if (!fbx_mat) continue;

		ImportMaterial& mat = m_materials.emplace();
		mat.fbx = fbx_mat;

		auto gatherTexture = [&mat, src_dir](ofbx::Texture::TextureType type) {
			const ofbx::Texture* texture = mat.fbx->getTexture(type);
			if (!texture) return;


			ImportTexture& tex = mat.textures[type];
			tex.fbx = texture;
			ofbx::DataView filename = tex.fbx->getRelativeFileName();
			if (filename == "") filename = tex.fbx->getFileName();
			filename.toString(tex.path.data);
			tex.src = tex.path;
			tex.is_valid = OS::fileExists(tex.src);

			if (!tex.is_valid)
			{
				PathInfo file_info(tex.path);
				tex.src = src_dir;
				tex.src << file_info.m_basename << "." << file_info.m_extension;
				tex.is_valid = OS::fileExists(tex.src);

				if (!tex.is_valid)
				{
					tex.src = src_dir;
					tex.src << tex.path;
					tex.is_valid = OS::fileExists(tex.src);
					
					if (!tex.is_valid) {
						tex.src = src_dir;
						tex.src << "textures/" << file_info.m_basename << "." << file_info.m_extension;
						tex.is_valid = OS::fileExists(tex.src);
					}
				}
			}

			char tmp[MAX_PATH_LENGTH];
			Path::normalize(tex.src, Span(tmp));
			tex.src = tmp;

			tex.import = true;
		};

		gatherTexture(ofbx::Texture::DIFFUSE);
		gatherTexture(ofbx::Texture::NORMAL);
		gatherTexture(ofbx::Texture::SPECULAR);
	}
}


void FBXImporter::insertHierarchy(Array<const ofbx::Object*>& bones, const ofbx::Object* node)
{
	if (!node) return;
	if (bones.indexOf(node) >= 0) return;
	ofbx::Object* parent = node->getParent();
	insertHierarchy(bones, parent);
	bones.push(node);
}


void FBXImporter::sortBones()
{
	int count = m_bones.size();
	for (int i = 0; i < count; ++i)
	{
		for (int j = i + 1; j < count; ++j)
		{
			if (m_bones[i]->getParent() == m_bones[j])
			{
				const ofbx::Object* bone = m_bones[j];
				m_bones.swapAndPop(j);
				m_bones.insert(i, bone);
				--i;
				break;
			}
		}
	}

	for (const ofbx::Object*& bone : m_bones) {
		const int idx = m_meshes.find([&](const ImportMesh& mesh){
			return mesh.fbx == bone;
		});

		if (idx >= 0) {
			m_meshes[idx].is_skinned = true;
			m_meshes[idx].bone_idx = int(&bone - m_bones.begin());
		}
	}
}


void FBXImporter::gatherBones(const ofbx::IScene& scene)
{
	for (const ImportMesh& mesh : m_meshes)
	{
		if(mesh.fbx->getGeometry()) {
			const ofbx::Skin* skin = mesh.fbx->getGeometry()->getSkin();
			if (skin)
			{
				for (int i = 0; i < skin->getClusterCount(); ++i)
				{
					const ofbx::Cluster* cluster = skin->getCluster(i);
					insertHierarchy(m_bones, cluster->getLink());
				}
			}
		}
	}

	for (int i = 0, n = scene.getAnimationStackCount(); i < n; ++i)
	{
		const ofbx::AnimationStack* stack = scene.getAnimationStack(i);
		for (int j = 0; stack->getLayer(j); ++j)
		{
			const ofbx::AnimationLayer* layer = stack->getLayer(j);
			for (int k = 0; layer->getCurveNode(k); ++k)
			{
				const ofbx::AnimationCurveNode* node = layer->getCurveNode(k);
				if (node->getBone()) insertHierarchy(m_bones, node->getBone());
			}
		}
	}

	m_bones.removeDuplicates();
	sortBones();
}


void FBXImporter::gatherAnimations(const ofbx::IScene& scene)
{
	int anim_count = scene.getAnimationStackCount();
	for (int i = 0; i < anim_count; ++i)
	{
		ImportAnimation& anim = m_animations.emplace();
		anim.scene = &scene;
		anim.fbx = (const ofbx::AnimationStack*)scene.getAnimationStack(i);
		anim.import = true;
		const ofbx::TakeInfo* take_info = scene.getTakeInfo(anim.fbx->name);
		if (take_info)
		{
			if (take_info->name.begin != take_info->name.end)
			{
				take_info->name.toString(anim.name.data);
			}
			if (anim.name.empty() && take_info->filename.begin != take_info->filename.end)
			{
				char tmp[MAX_PATH_LENGTH];
				take_info->filename.toString(tmp);
				Path::getBasename(Span(anim.name.data), tmp);
			}
			if (anim.name.empty()) anim.name << "anim";
		}
		else
		{
			anim.name = "anim";
		}
	}
}


static int findSubblobIndex(const OutputMemoryStream& haystack, const OutputMemoryStream& needle, const Array<int>& subblobs, int first_subblob)
{
	const u8* data = (const u8*)haystack.data();
	const u8* needle_data = (const u8*)needle.data();
	int step_size = (int)needle.size();
	int idx = first_subblob;
	while(idx != -1)
	{
		if (memcmp(data + idx * step_size, needle_data, step_size) == 0) return idx;
		idx = subblobs[idx];
	}
	return -1;
}


static Vec3 toLumixVec3(const ofbx::Vec3& v) { return {(float)v.x, (float)v.y, (float)v.z}; }


static Matrix toLumix(const ofbx::Matrix& mtx)
{
	Matrix res;

	for (int i = 0; i < 16; ++i) (&res.m11)[i] = (float)mtx.m[i];

	return res;
}


static u32 packF4u(const Vec3& vec)
{
	const i8 xx = i8(clamp((vec.x * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);
	const i8 yy = i8(clamp((vec.y * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);
	const i8 zz = i8(clamp((vec.z * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);
	const i8 ww = i8(0);

	union {
		u32 ui32;
		i8 arr[4];
	} un;

	un.arr[0] = xx;
	un.arr[1] = yy;
	un.arr[2] = zz;
	un.arr[3] = ww;

	return un.ui32;
}


void FBXImporter::writePackedVec3(const ofbx::Vec3& vec, const Matrix& mtx, OutputMemoryStream* blob) const
{
	Vec3 v = toLumixVec3(vec);
	v = (mtx * Vec4(v, 0)).xyz();
	v.normalize();
	v = fixOrientation(v);

	u32 packed = packF4u(v);
	blob->write(packed);
}


static void writeUV(const ofbx::Vec2& uv, OutputMemoryStream* blob)
{
	Vec2 tex_cooords = {(float)uv.x, 1 - (float)uv.y};
	blob->write(tex_cooords);
}


static void writeColor(const ofbx::Vec4& color, OutputMemoryStream* blob)
{
	u8 rgba[4];
	rgba[0] = u8(color.x * 255);
	rgba[1] = u8(color.y * 255);
	rgba[2] = u8(color.z * 255);
	rgba[3] = u8(color.w * 255);
	blob->write(rgba);
}


static void writeSkin(const FBXImporter::Skin& skin, OutputMemoryStream* blob)
{
	blob->write(skin.joints);
	blob->write(skin.weights);
	float sum = skin.weights[0] + skin.weights[1] + skin.weights[2] + skin.weights[3];
	ASSERT(sum > 0.99f && sum < 1.01f);
}


static int getMaterialIndex(const ofbx::Mesh& mesh, const ofbx::Material& material)
{
	for (int i = 0, c = mesh.getMaterialCount(); i < c; ++i)
	{
		if (mesh.getMaterial(i) == &material) return i;
	}
	return -1;
}


static void centerMesh(const ofbx::Vec3* vertices, int vertices_count, FBXImporter::ImportConfig::Origin origin, Matrix* transform)
{
	if (vertices_count <= 0) return;

	ofbx::Vec3 min = vertices[0];
	ofbx::Vec3 max = vertices[0];

	for (int i = 1; i < vertices_count; ++i)
	{
		ofbx::Vec3 v = vertices[i];
			
		min.x = minimum(min.x, v.x);
		min.y = minimum(min.y, v.y);
		min.z = minimum(min.z, v.z);
			
		max.x = maximum(max.x, v.x);
		max.y = maximum(max.y, v.y);
		max.z = maximum(max.z, v.z);
	}

	Vec3 center;
	center.x = float(min.x + max.x) * 0.5f;
	center.y = float(min.y + max.y) * 0.5f;
	center.z = float(min.z + max.z) * 0.5f;
		
	if (origin == FBXImporter::ImportConfig::Origin::BOTTOM) center.y = (float)min.y;

	transform->setTranslation(-center);
}


static ofbx::Vec3 operator-(const ofbx::Vec3& a, const ofbx::Vec3& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}


static ofbx::Vec2 operator-(const ofbx::Vec2& a, const ofbx::Vec2& b)
{
	return {a.x - b.x, a.y - b.y};
}


static void computeTangents(Array<ofbx::Vec3>& out, i32 vertex_count, const ofbx::Vec3* vertices, const ofbx::Vec3* normals, const ofbx::Vec2* uvs, const char* path)
{
	out.resize(vertex_count);

	struct {
		Array<ofbx::Vec3>* out;
		i32 vertex_count;
		const ofbx::Vec3* vertices;
		const ofbx::Vec3* normals;
		const ofbx::Vec2* uvs;
	} data;

	data.out = &out;
	data.vertex_count = vertex_count;
	data.vertices = vertices;
	data.normals = normals;
	data.uvs = uvs;

	SMikkTSpaceInterface iface = {};
	iface.m_getNumFaces = [](const SMikkTSpaceContext * pContext) -> int {
		auto* ptr = (decltype(data)*)pContext->m_pUserData;
		return ptr->vertex_count / 3;
	};
	iface.m_getNumVerticesOfFace = [](const SMikkTSpaceContext * pContext, const int face) -> int { return 3; };
	iface.m_getPosition = [](const SMikkTSpaceContext * pContext, float fvPosOut[], const int iFace, const int iVert) { 
		auto* ptr = (decltype(data)*)pContext->m_pUserData;
		ofbx::Vec3 p = ptr->vertices[iFace * 3 + iVert];
		fvPosOut[0] = (float)p.x;
		fvPosOut[1] = (float)p.y;
		fvPosOut[2] = (float)p.z;
	};
	iface.m_getNormal = [](const SMikkTSpaceContext * pContext, float fvNormOut[], const int iFace, const int iVert) { 
		auto* ptr = (decltype(data)*)pContext->m_pUserData;
		ofbx::Vec3 p = ptr->normals[iFace * 3 + iVert];
		fvNormOut[0] = (float)p.x;
		fvNormOut[1] = (float)p.y;
		fvNormOut[2] = (float)p.z;
	};
	iface.m_getTexCoord = [](const SMikkTSpaceContext * pContext, float fvTexcOut[], const int iFace, const int iVert) { 
		auto* ptr = (decltype(data)*)pContext->m_pUserData;
		ofbx::Vec2 p = ptr->uvs[iFace * 3 + iVert];
		fvTexcOut[0] = (float)p.x;
		fvTexcOut[1] = (float)p.y;
	};
	iface.m_setTSpaceBasic  = [](const SMikkTSpaceContext * pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert) {
		auto* ptr = (decltype(data)*)pContext->m_pUserData;
		ofbx::Vec3 t;
		t.x = fvTangent[0];
		t.y = fvTangent[1];
		t.z = fvTangent[2];
		(*ptr->out)[iFace * 3 + iVert] = t;
	};

	SMikkTSpaceContext ctx;
	ctx.m_pUserData = &data;
	ctx.m_pInterface = &iface;
	tbool res = genTangSpaceDefault(&ctx);
	if (!res) {
		logError("Renderer") << path << ": failed to generate tangent space";
	}
}

static bool doesFlipHandness(const Matrix& mtx) {
	Vec3 x(1, 0, 0);
	Vec3 y(0, 1, 0);
	Vec3 z = mtx.inverted().transformVector(crossProduct(mtx.transformVector(x), mtx.transformVector(y)));
	return z.z < 0;
}

void FBXImporter::postprocessMeshes(const ImportConfig& cfg, const char* path)
{
	JobSystem::forEach(m_meshes.size(), 1, [&](i32 mesh_idx, i32){
		ImportMesh& import_mesh = m_meshes[mesh_idx];
		import_mesh.vertex_data.clear();
		import_mesh.indices.clear();

		const ofbx::Mesh& mesh = *import_mesh.fbx;
		const ofbx::Geometry* geom = import_mesh.fbx->getGeometry();
		int vertex_count = geom->getVertexCount();
		const ofbx::Vec3* vertices = geom->getVertices();
		const ofbx::Vec3* normals = geom->getNormals();
		const ofbx::Vec3* tangents = geom->getTangents();
		const ofbx::Vec4* colors = m_import_vertex_colors ? geom->getColors() : nullptr;
		const ofbx::Vec2* uvs = geom->getUVs();

		Matrix transform_matrix = Matrix::IDENTITY;
		Matrix geometry_matrix = toLumix(mesh.getGeometricMatrix());
		transform_matrix = toLumix(mesh.getGlobalTransform()) * geometry_matrix;
		if (cancel_mesh_transforms) transform_matrix.setTranslation({0, 0, 0});
		if (cfg.origin != ImportConfig::Origin::SOURCE) {
			centerMesh(vertices, vertex_count, cfg.origin, &transform_matrix);
		}
		import_mesh.transform_matrix = transform_matrix;
		import_mesh.transform_matrix.inverse();

		const bool flip_handness = doesFlipHandness(transform_matrix);
		if (flip_handness) {
			logError("FBX") << "Mesh " << mesh.name << " in " << path << " flips handness. This is not supported and the mesh will not display correctly.";
		}

		OutputMemoryStream blob(m_allocator);
		int vertex_size = getVertexSize(import_mesh);
		import_mesh.vertex_data.reserve(vertex_count * vertex_size);

		Array<Skin> skinning(m_allocator);
		if (import_mesh.is_skinned) fillSkinInfo(skinning, import_mesh);

		AABB aabb = {{FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX}};
		float radius_squared = 0;

		int material_idx = getMaterialIndex(mesh, *import_mesh.fbx_mat);
		ASSERT(material_idx >= 0);

		int first_subblob[256];
		for (int& subblob : first_subblob) subblob = -1;
		Array<int> subblobs(m_allocator);
		subblobs.reserve(vertex_count);

		const int* geom_materials = geom->getMaterials();
		Array<ofbx::Vec3> computed_tangents(m_allocator);
		if (!tangents && normals && uvs) {
			computeTangents(computed_tangents, vertex_count, vertices, normals, uvs, path);
			tangents = computed_tangents.begin();
		}

		for (int i = 0; i < vertex_count; ++i)
		{
			if (geom_materials && geom_materials[i / 3] != material_idx) continue;

			blob.clear();
			ofbx::Vec3 cp = vertices[i];
			// premultiply control points here, so we can have constantly-scaled meshes without scale in bones
			Vec3 pos = transform_matrix.transformPoint(toLumixVec3(cp)) * cfg.mesh_scale * m_fbx_scale;
			pos = fixOrientation(pos);
			blob.write(pos);

			float sq_len = pos.squaredLength();
			radius_squared = maximum(radius_squared, sq_len);

			aabb.min.x = minimum(aabb.min.x, pos.x);
			aabb.min.y = minimum(aabb.min.y, pos.y);
			aabb.min.z = minimum(aabb.min.z, pos.z);
			aabb.max.x = maximum(aabb.max.x, pos.x);
			aabb.max.y = maximum(aabb.max.y, pos.y);
			aabb.max.z = maximum(aabb.max.z, pos.z);

			if (normals) writePackedVec3(normals[i], transform_matrix, &blob);
			if (uvs) writeUV(uvs[i], &blob);
			if (colors) writeColor(colors[i], &blob);
			if (tangents) writePackedVec3(tangents[i], transform_matrix, &blob);
			if (import_mesh.is_skinned) writeSkin(skinning[i], &blob);

			u8 first_byte = blob.data()[0] ^ blob.data()[1] ^ blob.data()[4] ^ blob.data()[8];

			int idx = findSubblobIndex(import_mesh.vertex_data, blob, subblobs, first_subblob[first_byte]);
			if (idx == -1)
			{
				subblobs.push(first_subblob[first_byte]);
				first_subblob[first_byte] = subblobs.size() - 1;
				import_mesh.indices.push((int)import_mesh.vertex_data.size() / vertex_size);
				import_mesh.vertex_data.write(blob.data(), vertex_size);
			}
			else
			{
				import_mesh.indices.push(idx);
			}
		}

		import_mesh.aabb = aabb;
		import_mesh.radius_squared = radius_squared;
	});
	for (int mesh_idx = m_meshes.size() - 1; mesh_idx >= 0; --mesh_idx)
	{
		if (m_meshes[mesh_idx].indices.empty()) m_meshes.swapAndPop(mesh_idx);
	}
}


static int detectMeshLOD(const FBXImporter::ImportMesh& mesh)
{
	const char* node_name = mesh.fbx->name;
	const char* lod_str = stristr(node_name, "_LOD");
	if (!lod_str)
	{
		char mesh_name[256];
		FBXImporter::getImportMeshName(mesh, mesh_name);
		lod_str = stristr(mesh_name, "_LOD");
		if (!lod_str) return 0;
	}

	lod_str += stringLength("_LOD");

	int lod;
	fromCString(Span(lod_str, stringLength(lod_str)), Ref(lod));

	return lod;
}


void FBXImporter::gatherMeshes(ofbx::IScene* scene)
{
	int min_lod = 2;
	int c = scene->getMeshCount();
	int start_index = m_meshes.size();
	for (int i = 0; i < c; ++i) {
		const ofbx::Mesh* fbx_mesh = (const ofbx::Mesh*)scene->getMesh(i);
		const int mat_count = fbx_mesh->getMaterialCount();
		for (int j = 0; j < mat_count; ++j) {
			ImportMesh& mesh = m_meshes.emplace(m_allocator);
			mesh.is_skinned = false;
			if (fbx_mesh->getGeometry() && fbx_mesh->getGeometry()->getSkin()) {
				const ofbx::Skin* skin = fbx_mesh->getGeometry()->getSkin();
				for (int i = 0; i < skin->getClusterCount(); ++i) {
					if (skin->getCluster(i)->getIndicesCount() > 0) {
						mesh.is_skinned = true;
						break;
					}
				}
			}
			mesh.fbx = fbx_mesh;
			mesh.fbx_mat = fbx_mesh->getMaterial(j);
			mesh.submesh = mat_count > 1 ? j : -1;
			mesh.lod = detectMeshLOD(mesh);
			min_lod = minimum(min_lod, mesh.lod);
		}
	}
	if (min_lod != 1) return;
	for (int i = start_index, n = m_meshes.size(); i < n; ++i) {
		--m_meshes[i].lod;
	}
}


FBXImporter::~FBXImporter()
{
	if (scene) scene->destroy();
}


FBXImporter::FBXImporter(StudioApp& app)
	: m_allocator(app.getAllocator())
	, m_compiler(app.getAssetCompiler())
	, scene(nullptr)
	, m_materials(m_allocator)
	, m_meshes(m_allocator)
	, m_animations(m_allocator)
	, m_bones(m_allocator)
	, out_file(m_allocator)
	, m_filesystem(app.getEngine().getFileSystem())
	, m_app(app)
{
}


static void ofbx_job_processor(ofbx::JobFunction fn, void*, void* data, u32 size, u32 count) {
	JobSystem::forEach(count, 1, [data, size, fn](i32 i, i32){
		u8* ptr = (u8*)data;
		fn(ptr + i * size);
	});
}


bool FBXImporter::setSource(const char* filename, bool ignore_geometry)
{
	out_file.reserve(1024 * 1024);
	PROFILE_FUNCTION();
	if(scene) {
		scene->destroy();
		scene = nullptr;	
		m_meshes.clear();
		m_materials.clear();
		m_animations.clear();
		m_bones.clear();
	}

	OutputMemoryStream data(m_allocator);
	if (!m_filesystem.getContentSync(Path(filename), Ref(data))) return false;
	
	const u64 flags = ignore_geometry ? (u64)ofbx::LoadFlags::IGNORE_GEOMETRY : (u64)ofbx::LoadFlags::TRIANGULATE;
	scene = ofbx::load(data.data(), (i32)data.size(), flags, &ofbx_job_processor, nullptr);
	if (!scene)
	{
		logError("FBX") << "Failed to import \"" << filename << ": " << ofbx::getError() << "\n"
			<< "Please try to convert the FBX file with Autodesk FBX Converter or some other software to the latest version.";
		return false;
	}
	m_fbx_scale = scene->getGlobalSettings()->UnitScaleFactor * 0.01f;

	const ofbx::GlobalSettings* settings = scene->getGlobalSettings();
	switch (settings->UpAxis) {
		case ofbx::UpVector_AxisX: m_orientation = Orientation::X_UP; break;
		case ofbx::UpVector_AxisY: m_orientation = Orientation::Y_UP; break;
		case ofbx::UpVector_AxisZ: m_orientation = Orientation::Z_UP; break;
	}

	char src_dir[MAX_PATH_LENGTH];
	Path::getDir(Span(src_dir), filename);
	if (!ignore_geometry) extractEmbedded(*scene, src_dir);
	gatherMeshes(scene);

	gatherAnimations(*scene);
	if (!ignore_geometry) {
		gatherMaterials(src_dir);
		m_materials.removeDuplicates([](const ImportMaterial& a, const ImportMaterial& b) { return a.fbx == b.fbx; });
		gatherBones(*scene);
	}

	return true;
}


void FBXImporter::writeString(const char* str) { out_file.write(str, stringLength(str)); }

static Vec3 impostorToWorld(Vec2 uv) {
	Vec3 position = Vec3(
						0.0f + (uv.x - uv.y),
						-1.0f + (uv.x + uv.y),
						0.0f
					);

	Vec2 absolute;
	absolute.x = fabsf(position.x);
	absolute.y = fabsf(position.y);
	position.z = 1.0f - absolute.x - absolute.y;

	return Vec3{position.x, position.z, position.y};
};

static constexpr u32 IMPOSTOR_TILE_SIZE = 512;
static constexpr u32 IMPOSTOR_COLS = 9;

static void getBBProjection(const AABB& aabb, Ref<Vec2> out_min, Ref<Vec2> out_max) {
	const float radius = (aabb.max - aabb.min).length() * 0.5f;
	const Vec3 center = (aabb.min + aabb.max) * 0.5f;

	Matrix proj;
	proj.setOrtho(-1, 1, -1, 1, 0, radius * 2, false, true);
	Vec2 min(FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX);
	for (u32 j = 0; j < IMPOSTOR_COLS; ++j) {
		for (u32 i = 0; i < IMPOSTOR_COLS; ++i) {
			const Vec3 v = impostorToWorld({i / (float)(IMPOSTOR_COLS - 1), j / (float)(IMPOSTOR_COLS - 1)});
			Matrix view;
			view.lookAt(center + v, center, Vec3(0, 1, 0));
			const Matrix vp = proj * view;
			for (u32 k = 0; k < 8; ++k) {
				const Vec3 p = {
					k & 1 ? aabb.min.x : aabb.max.x,
					k & 2 ? aabb.min.y : aabb.max.y,
					k & 4 ? aabb.min.z : aabb.max.z
				};
				const Vec4 proj_p = vp * Vec4(p, 1);
				min.x = minimum(min.x, proj_p.x / proj_p.w);
				min.y = minimum(min.y, proj_p.y / proj_p.w);
				max.x = maximum(max.x, proj_p.x / proj_p.w);
				max.y = maximum(max.y, proj_p.y / proj_p.w);
			}
		}
	}
	out_min = min;
	out_max = max;
}

struct CaptureImpostorJob : Renderer::RenderJob {

	CaptureImpostorJob(Ref<Array<u32>> gb0, Ref<Array<u32>> gb1, Ref<IVec2> size, IAllocator& allocator) 
		: m_gb0(gb0)
		, m_gb1(gb1)
		, m_tile_size(size)
		, m_drawcalls(allocator)
	{
	}

	void setup() override {
		m_aabb = m_model->getAABB();
		m_radius = m_model->getBoundingRadius();
		for (u32 i = 0; i <= (u32)m_model->getLODs()[0].to_mesh; ++i) {
			const Mesh& mesh = m_model->getMesh(i);
			Shader* shader = mesh.material->getShader();
			Drawcall& dc = m_drawcalls.emplace();
			dc.program = shader->getProgram(mesh.vertex_decl, m_capture_define | mesh.material->getDefineMask());
			dc.mesh = mesh.render_data;
			dc.material = mesh.material->getRenderData();
		}
	}

	void execute() override {
		gpu::TextureHandle gbs[] = { gpu::allocTextureHandle(), gpu::allocTextureHandle(), gpu::allocTextureHandle() };

		gpu::BufferHandle pass_buf = gpu::allocBufferHandle();
		gpu::BufferHandle ub = gpu::allocBufferHandle();
		gpu::createBuffer(ub, (u32)gpu::BufferFlags::UNIFORM_BUFFER, 256, nullptr);
		const u32 pass_buf_size = (sizeof(PassState) + 255) & ~255;
		gpu::createBuffer(pass_buf, (u32)gpu::BufferFlags::UNIFORM_BUFFER, pass_buf_size, nullptr);
		gpu::bindUniformBuffer(1, pass_buf, pass_buf_size);
		gpu::bindUniformBuffer(4, ub, 256);

		const Vec3 center = (m_aabb.min + m_aabb.max) * 0.5f;
		Vec2 min, max;
		getBBProjection(m_aabb, Ref(min), Ref(max));
		const Vec2 size = max - min;

		m_tile_size = IVec2(int(IMPOSTOR_TILE_SIZE * size.x / size.y), IMPOSTOR_TILE_SIZE);
		m_tile_size->x = (m_tile_size->x + 3) & ~3;
		m_tile_size->y = (m_tile_size->y + 3) & ~3;
		const IVec2 texture_size = m_tile_size.value * IMPOSTOR_COLS;
		gpu::createTexture(gbs[0], texture_size.x, texture_size.y, 1, gpu::TextureFormat::RGBA8, (u32)gpu::TextureFlags::NO_MIPS, nullptr, "impostor_gb0");
		gpu::createTexture(gbs[1], texture_size.x, texture_size.y, 1, gpu::TextureFormat::RGBA8, (u32)gpu::TextureFlags::NO_MIPS, nullptr, "impostor_gb1");
		gpu::createTexture(gbs[2], texture_size.x, texture_size.y, 1, gpu::TextureFormat::D24S8, (u32)gpu::TextureFlags::NO_MIPS, nullptr, "impostor_gbd");
		
		gpu::setFramebuffer(gbs, 3, 0);
		const float color[] = {0, 0, 0, 0};
		gpu::clear((u32)gpu::ClearFlags::COLOR | (u32)gpu::ClearFlags::DEPTH | (u32)gpu::ClearFlags::STENCIL, color, 0);

		for (u32 j = 0; j < IMPOSTOR_COLS; ++j) {
			for (u32 i = 0; i < IMPOSTOR_COLS; ++i) {
				gpu::viewport(i * m_tile_size->x, j * m_tile_size->y, m_tile_size->x, m_tile_size->y);
				for (const Drawcall& dc : m_drawcalls) {
					const Mesh::RenderData* rd = dc.mesh;
					gpu::bindTextures(dc.material->textures, 0, dc.material->textures_count);

					const Vec3 v = impostorToWorld({i / (float)(IMPOSTOR_COLS - 1), j / (float)(IMPOSTOR_COLS - 1)});

					Matrix model_mtx;
					if (i == IMPOSTOR_COLS >> 1 && j == IMPOSTOR_COLS >> 1) {
						model_mtx.lookAt(Vec3(0, 0, 0), v, Vec3(0, 0, 1));
					}
					else {
						model_mtx.lookAt(Vec3(0, 0, 0), v, Vec3(0, 1, 0));
					}
					gpu::update(ub, &model_mtx.m11, sizeof(model_mtx));
					PassState pass_state;
					pass_state.view.lookAt(center + Vec3(0, 0, 2 * m_radius), center, {0, 1, 0});
					pass_state.projection.setOrtho(min.x, max.x, min.y, max.y, 0, 5 * m_radius, false, true);
					pass_state.inv_projection = pass_state.projection.inverted();
					pass_state.inv_view = pass_state.view.fastInverted();
					pass_state.view_projection = pass_state.projection * pass_state.view;
					pass_state.inv_view_projection = pass_state.view_projection.inverted();
					pass_state.view_dir = Vec4(pass_state.view.inverted().transformVector(Vec3(0, 0, -1)), 0);
					gpu::update(pass_buf, &pass_state, sizeof(pass_state));


					gpu::useProgram(dc.program);
					gpu::bindUniformBuffer(2, m_material_ub, dc.material->material_constants);
					gpu::bindIndexBuffer(rd->index_buffer_handle);
					gpu::bindVertexBuffer(0, rd->vertex_buffer_handle, 0, rd->vb_stride);
					gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
					gpu::setState(u64(gpu::StateFlags::DEPTH_TEST) | u64(gpu::StateFlags::DEPTH_WRITE) | dc.material->render_states);
					gpu::drawTriangles(rd->indices_count, rd->index_type);
				}
			}
		}

		gpu::setFramebuffer(nullptr, 0, 0);

		m_gb0->resize(texture_size.x * texture_size.y);
		m_gb1->resize(m_gb0->size());

		gpu::TextureHandle staging = gpu::allocTextureHandle();
		const u32 flags = u32(gpu::TextureFlags::NO_MIPS) | u32(gpu::TextureFlags::READBACK);
		gpu::createTexture(staging, texture_size.x, texture_size.y, 1, gpu::TextureFormat::RGBA8, flags, nullptr, "staging_buffer");
		gpu::copy(staging, gbs[0], 0, 0);
		gpu::readTexture(staging, 0, Span((u8*)m_gb0->begin(), m_gb0->byte_size()));
		gpu::copy(staging, gbs[1], 0, 0);
		gpu::readTexture(staging, 0, Span((u8*)m_gb1->begin(), m_gb1->byte_size()));
		gpu::destroy(staging);

		gpu::destroy(ub);
		gpu::destroy(gbs[0]);
		gpu::destroy(gbs[1]);
		gpu::destroy(gbs[2]);
	}

	struct Drawcall {
		gpu::ProgramHandle program;
		const Mesh::RenderData* mesh;
		const Material::RenderData* material;
	};

	Array<Drawcall> m_drawcalls;
	AABB m_aabb;
	float m_radius;
	gpu::BufferGroupHandle m_material_ub;
	Ref<Array<u32>> m_gb0;
	Ref<Array<u32>> m_gb1;
	Model* m_model;
	u32 m_capture_define;
	Ref<IVec2> m_tile_size;
};

bool FBXImporter::createImpostorTextures(Model* model, Ref<Array<u32>> gb0_rgba, Ref<Array<u32>> gb1_rgba, Ref<IVec2> size)
{
	ASSERT(model->isReady());

	Engine& engine = m_app.getEngine();
	Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
	ASSERT(renderer);

	IAllocator& allocator = renderer->getAllocator();
	CaptureImpostorJob* job = LUMIX_NEW(allocator, CaptureImpostorJob)(gb0_rgba, gb1_rgba, size, allocator);
	job->m_model = model;
	job->m_capture_define = 1 << renderer->getShaderDefineIdx("DEFERRED");
	job->m_material_ub = renderer->getMaterialUniformBuffer();
	renderer->queue(job, 0);
	renderer->frame();
	renderer->waitForRender();
	
	return true;
}


void FBXImporter::writeMaterials(const char* src, const ImportConfig& cfg)
{
	PROFILE_FUNCTION()
	const PathInfo src_info(src);
	for (const ImportMaterial& material : m_materials) {
		if (!material.import) continue;

		char mat_name[128];
		getMaterialName(material.fbx, mat_name);

		const StaticString<MAX_PATH_LENGTH + 128> mat_src(src_info.m_dir, mat_name, ".mat");
		if (m_filesystem.fileExists(mat_src)) continue;

		OS::OutputFile f;
		if (!m_filesystem.open(mat_src, Ref(f)))
		{
			logError("FBX") << "Failed to create " << mat_src;
			continue;
		}
		out_file.clear();

		writeString("shader \"pipelines/standard.shd\"\n");
		if (material.alpha_cutout) writeString("defines {\"ALPHA_CUTOUT\"}\n");
		if (material.textures[2].is_valid) writeString("metallic(1.000000)");

		auto writeTexture = [this](const ImportTexture& texture, u32 idx) {
			if (texture.is_valid && idx < 2) {
				const StaticString<MAX_PATH_LENGTH> meta_path(texture.src, ".meta");
				if (!OS::fileExists(meta_path)) {
					OS::OutputFile file;
					if (file.open(meta_path)) {
						
						file << (idx == 0 ? "srgb = true\n" : "normalmap = true\n");
						file.close();
					}
				}
			}
			if (texture.fbx)
			{
				writeString("texture \"/");
				writeString(texture.src);
				writeString("\"\n");
			}
			else
			{
				writeString("texture \"\"\n");
			}
		};

		writeTexture(material.textures[0], 0);
		writeTexture(material.textures[1], 1);
		writeTexture(material.textures[2], 2);
		
/*			ofbx::Color diffuse_color = material.fbx->getDiffuseColor();
		out_file << "color {" << diffuse_color.r 
			<< "," << diffuse_color.g
			<< "," << diffuse_color.b
			<< ",1}\n";*/

		if (!f.write(out_file.data(), out_file.size())) {
			logError("FBX") << "Failed to write " << mat_src;
		}
		f.close();
	}

	if (cfg.create_impostor) {
		const StaticString<MAX_PATH_LENGTH> mat_src(src_info.m_dir, src_info.m_basename, "_impostor.mat");
		if (!m_filesystem.fileExists(mat_src)) {
			OS::OutputFile f;
			if (!m_filesystem.open(mat_src, Ref(f))) {
				logError("FBX") << "Failed to create " << mat_src;
			}
			else {
				f << "shader \"/pipelines/impostor.shd\"\n";
				f << "texture \"" << src_info.m_basename << "_impostor0.tga\"\n";
				f << "texture \"" << src_info.m_basename << "_impostor1.tga\"\n";
				f << "defines { \"ALPHA_CUTOUT\" }\n";
				f << "backface_culling(false)\n";
				f.close();
			}
		}
	}
}

static void convert(const ofbx::Matrix& mtx, Ref<Vec3> pos, Ref<Quat> rot)
{
	Matrix m = toLumix(mtx);
	m.normalizeScale();
	rot = m.getRotation();
	pos = m.getTranslation();
}

template <typename T>
static void fillTimes(const ofbx::AnimationCurve* curve, Ref<Array<T>> out){
	if(!curve) return;

	const i64* times = curve->getKeyTime();
	for (u32 i = 0, c = curve->getKeyCount(); i < c; ++i) {
		if (out->empty()) {
			out->emplace().time = times[i];
		}
		else if (times[i] > out->back().time) {
			out->emplace().time = times[i];
		}
		else {
			for (const T& k : out.value) {
				if (k.time == times[i]) break;
				if (times[i] < k.time) {
					const u32 idx = u32(&k - out->begin());
					out->emplaceAt(idx).time = times[i];
					break;
				}
			}
		}
	}
};

static float evalCurve(i64 time, const ofbx::AnimationCurve& curve) {
	const i64* times = curve.getKeyTime();
	const float* values = curve.getKeyValue();
	const int count = curve.getKeyCount();

	ASSERT(count > 0);

	time = clamp(time, times[0], times[count - 1]);

	for (int i = 0; i < count; ++i) {
		if (time == times[i]) return values[i];
		if (time < times[i]) {
			ASSERT(i > 0);
			ASSERT(time > times[i - 1]);
			const float t = float((time - times[i - 1]) / double(times[i] - times[i - 1]));
			return values[i - 1] * (1 - t) + values[i] * t;
		}
	}
	ASSERT(false);
	return 0.f;
};

// parent_scale - animated scale is not supported, but we can get rid of static scale if we ignore
// it in writeSkeleton() and use `parent_scale` in this function
static void compressPositions(float error, float parent_scale, Ref<Array<FBXImporter::Key>> keys)
{
	Array<FBXImporter::Key>& out = keys.value;
	if (out.empty()) return;

	Vec3 dir = out[1].pos - out[0].pos;
	dir *= float(1 / ofbx::fbxTimeToSeconds(out[1].time - out[0].time));
	u32 prev = 0;
	for (u32 i = 2; i < (u32)out.size(); ++i) {
		const Vec3 estimate = out[prev].pos + dir * (float)ofbx::fbxTimeToSeconds(out[i].time - out[prev].time);
		const Vec3 diff = estimate - out[i].pos;
		if (fabs(diff.x) > error || fabs(diff.y) > error || fabs(diff.z) > error)  {
			prev = i - 1;
			dir = out[i].pos - out[i - 1].pos;
			dir *= float(1 / ofbx::fbxTimeToSeconds(out[i].time - out[i - 1].time));
		}
		else {
			out[i - 1].flags |= 1;
		}
	}
}

static void compressRotations(float error,
	Ref<Array<FBXImporter::Key>> keys)
{
	Array<FBXImporter::Key>& out = keys.value;
	if (out.empty()) return;

	u32 prev = 0;
	for (u32 i = 2; i < (u32)out.size(); ++i) {
		const float t = float(ofbx::fbxTimeToSeconds(out[prev + 1].time - out[prev].time) / ofbx::fbxTimeToSeconds(out[i].time - out[prev].time));
		const Quat estimate = nlerp(out[prev].rot, out[i].rot, t);
		if (fabs(estimate.x - out[prev + 1].rot.x) > error || fabs(estimate.y - out[prev + 1].rot.y) > error ||
			fabs(estimate.z - out[prev + 1].rot.z) > error) 
		{
			prev = i - 1;
		}
		else {
			out[i - 1].flags |= 2;
		}
	}
}

static float getScaleX(const ofbx::Matrix& mtx)
{
	Vec3 v(float(mtx.m[0]), float(mtx.m[4]), float(mtx.m[8]));

	return v.length();
}

static void fill(const ofbx::Object& bone, double anim_len, const ofbx::AnimationLayer& layer, Ref<Array<FBXImporter::Key>> keys) {
	const ofbx::AnimationCurveNode* translation_node = layer.getCurveNode(bone, "Lcl Translation");
	const ofbx::AnimationCurveNode* rotation_node = layer.getCurveNode(bone, "Lcl Rotation");
	if (!translation_node && !rotation_node) return;

	keys->emplace().time = 0;
	keys->emplace().time = ofbx::secondsToFbxTime(anim_len);
	if (translation_node) {
		fillTimes(translation_node->getCurve(0), keys);
		fillTimes(translation_node->getCurve(1), keys);
		fillTimes(translation_node->getCurve(2), keys);
	}

	if (rotation_node) {
		fillTimes(rotation_node->getCurve(0), keys);
		fillTimes(rotation_node->getCurve(1), keys);
		fillTimes(rotation_node->getCurve(2), keys);
	}
	
	auto fill_rot = [&](u32 idx, const ofbx::AnimationCurve* curve) {
		if (!curve) {
			const ofbx::Vec3 lcl_rot = bone.getLocalRotation();
			for (FBXImporter::Key& k : keys.value) {
				(&k.rot.x)[idx] = float((&lcl_rot.x)[idx]);
			}
			return;
		}

		for (FBXImporter::Key& k : keys.value) {
			(&k.rot.x)[idx] = evalCurve(k.time, *curve);
		}
	};
	
	auto fill_pos = [&](u32 idx, const ofbx::AnimationCurve* curve) {
		if (!curve) {
			const ofbx::Vec3 lcl_pos = bone.getLocalTranslation();
			for (FBXImporter::Key& k : keys.value) {
				(&k.pos.x)[idx] = float((&lcl_pos.x)[idx]);
			}
			return;
		}

		for (FBXImporter::Key& k : keys.value) {
			(&k.pos.x)[idx] = evalCurve(k.time, *curve);
		}
	};
	
	fill_rot(0, rotation_node ? rotation_node->getCurve(0) : nullptr);
	fill_rot(1, rotation_node ? rotation_node->getCurve(1) : nullptr);
	fill_rot(2, rotation_node ? rotation_node->getCurve(2) : nullptr);

	fill_pos(0, translation_node ? translation_node->getCurve(0) : nullptr);
	fill_pos(1, translation_node ? translation_node->getCurve(1) : nullptr);
	fill_pos(2, translation_node ? translation_node->getCurve(2) : nullptr);

	for (FBXImporter::Key& key : keys.value) {
		const ofbx::Matrix mtx = bone.evalLocal({key.pos.x, key.pos.y, key.pos.z}, {key.rot.x, key.rot.y, key.rot.z});
		convert(mtx, Ref(key.pos), Ref(key.rot));
	}

}

static bool shouldSample(u32 keyframe_count, float anim_len, float fps, u32 data_size) {
	const u32 sampled_frame_count = u32(anim_len * fps);
	const u32 sampled_size = sampled_frame_count * data_size;
	const u32 time_size = sizeof(u16);
	const u32 keyframed_size = keyframe_count * (data_size + time_size);

	// * 4 / 3 -> prefer sampled even when a bit bigger, since sampled tracks are faster
	return sampled_size < keyframed_size * 4 / 3; 
}

static LocalRigidTransform sample(const ofbx::Object& bone, const ofbx::AnimationLayer& layer, float t) {
	const ofbx::AnimationCurveNode* translation_node = layer.getCurveNode(bone, "Lcl Translation");
	const ofbx::AnimationCurveNode* rotation_node = layer.getCurveNode(bone, "Lcl Rotation");

	LocalRigidTransform res;
	res.pos = toLumixVec3(bone.getLocalTranslation());
	if (translation_node) {
		const ofbx::AnimationCurve* x_curve = translation_node->getCurve(0);
		const ofbx::AnimationCurve* y_curve = translation_node->getCurve(1);
		const ofbx::AnimationCurve* z_curve = translation_node->getCurve(2);
		if (x_curve) res.pos.x = evalCurve(ofbx::secondsToFbxTime(t), *x_curve);
		if (y_curve) res.pos.y = evalCurve(ofbx::secondsToFbxTime(t), *y_curve);
		if (z_curve) res.pos.z = evalCurve(ofbx::secondsToFbxTime(t), *z_curve);
	}

	Vec3 euler_angles;
	euler_angles = toLumixVec3(bone.getLocalRotation());
	if (rotation_node) {
		const ofbx::AnimationCurve* x_curve = rotation_node->getCurve(0);
		const ofbx::AnimationCurve* y_curve = rotation_node->getCurve(1);
		const ofbx::AnimationCurve* z_curve = rotation_node->getCurve(2);
		if (x_curve) euler_angles.x = evalCurve(ofbx::secondsToFbxTime(t), *x_curve);
		if (y_curve) euler_angles.y = evalCurve(ofbx::secondsToFbxTime(t), *y_curve);
		if (z_curve) euler_angles.z = evalCurve(ofbx::secondsToFbxTime(t), *z_curve);
	}
	
	const ofbx::Matrix mtx = bone.evalLocal({res.pos.x, res.pos.y, res.pos.z}, {euler_angles.x, euler_angles.y, euler_angles.z});
	convert(mtx, Ref(res.pos), Ref(res.rot));
	return res;
}

static bool isBindPosePositionTrack(u32 count, const Array<FBXImporter::Key>& keys, const ofbx::Object& bone, float error) {
	if (count != 2) return false;
	const Vec3 p = toLumixVec3(bone.getLocalTranslation());
	for (const FBXImporter::Key& key : keys) {
		if (key.flags & 1) continue;
		const Vec3 d = key.pos - p;
		if (d.x > error || d.y > error || d.z > error) return false;
	}
	return true;
}

void FBXImporter::writeAnimations(const char* src, const ImportConfig& cfg)
{
	PROFILE_FUNCTION();
	for (const FBXImporter::ImportAnimation& anim : getAnimations()) { 
		ASSERT(anim.import);

		const ofbx::AnimationStack* stack = anim.fbx;
		const ofbx::AnimationLayer* layer = stack->getLayer(0);
		ASSERT(anim.scene == scene);
		const float fps = scene->getSceneFrameRate();
		const ofbx::TakeInfo* take_info = scene->getTakeInfo(stack->name);
		if(!take_info && startsWith(stack->name, "AnimStack::")) {
			take_info = scene->getTakeInfo(stack->name + 11);
		}

		double anim_len;
		if (take_info) {
			anim_len = take_info->local_time_to - take_info->local_time_from;
		}
		else if(scene->getGlobalSettings()) {
			anim_len = scene->getGlobalSettings()->TimeSpanStop;
		}
		else {
			logError("Renderer") << "Unsupported animation in " << src;
			continue;
		}

		out_file.clear();

		Animation::Header header;
		header.magic = Animation::HEADER_MAGIC;
		header.version = 3;
		header.length = Time::fromSeconds((float)anim_len);
		header.frame_count = u32(anim_len * fps + 0.5f);
		write(header);

		write(anim.root_motion_bone_idx);

		Array<Array<Key>> all_keys(m_allocator);
		auto fbx_to_anim_time = [anim_len](i64 fbx_time){
			const double t = clamp(ofbx::fbxTimeToSeconds(fbx_time) / anim_len, 0.0, 1.0);
			return u16(t * 0xffFF);
		};

		all_keys.reserve(m_bones.size());
		for (const ofbx::Object* bone : m_bones) {
			Array<Key>& keys = all_keys.emplace(m_allocator);
			fill(*bone, anim_len, *layer, Ref(keys));
		}

		for (const ofbx::Object*& bone : m_bones) {
			Array<Key>& keys = all_keys[u32(&bone - m_bones.begin())];
			const float parent_scale = bone->getParent() ? (float)getScaleX(bone->getParent()->getGlobalTransform()) : 1;
			// TODO skip curves which do not change anything
			compressRotations(cfg.rotation_error, Ref(keys));
			compressPositions(cfg.position_error, parent_scale, Ref(keys));
		}

		const u64 stream_translations_count_pos = out_file.size();
		u32 translation_curves_count = 0;
		write(translation_curves_count);
		for (const ofbx::Object*& bone : m_bones) {
			Array<Key>& keys = all_keys[u32(&bone - m_bones.begin())];
			u32 count = 0;
			for (Key& key : keys) {
				if ((key.flags & 1) == 0) ++count;
			}
			if (count == 0) continue;
			if (isBindPosePositionTrack(count, keys, *bone, cfg.position_error)) continue;
			
			const u32 name_hash = crc32(bone->name);
			write(name_hash);
			write(Animation::CurveType::KEYFRAMED);
			write(count);

			for (Key& key : keys) {
				if ((key.flags & 1) == 0) {
					write(fbx_to_anim_time(key.time));
				}
			}
			for (Key& key : keys) {
				if ((key.flags & 1) == 0) {
					write(fixOrientation(key.pos * cfg.mesh_scale * m_fbx_scale));
				}
			}
			++translation_curves_count;
		}
		memcpy(out_file.getMutableData() + stream_translations_count_pos, &translation_curves_count, sizeof(translation_curves_count));

		const u64 stream_rotations_count_pos = out_file.size();
		u32 rotation_curves_count = 0;
		write(rotation_curves_count);
		u32 sampled_count = 0;

		for (const ofbx::Object*& bone : m_bones) {
			Array<Key>& keys = all_keys[u32(&bone - m_bones.begin())];
			u32 count = 0;
			for (Key& key : keys) {
				if ((key.flags & 2) == 0) ++count;
			}
			if (count == 0) continue;

			const u32 name_hash = crc32(bone->name);
			write(name_hash);
			if (shouldSample(count, float(anim_len), fps, sizeof(Quat))) {
				++sampled_count;
				write(Animation::CurveType::SAMPLED);
				count = u32(anim_len * fps + 0.5f);
				write(count);
				for (u32 i = 0; i < count; ++i) {
					const float t = float(anim_len * ((float)i / (count - 1)));
					write(fixOrientation(sample(*bone, *layer, t).rot));
				}
			}
			else {
				write(Animation::CurveType::KEYFRAMED);
				write(count);
				for (Key& key : keys) {
					if ((key.flags & 2) == 0) {
						write(fbx_to_anim_time(key.time));
					}
				}
				for (Key& key : keys) {
					if ((key.flags & 2) == 0) {
						write(fixOrientation(key.rot));
					}
				}
			}
			++rotation_curves_count;
		}
		memcpy(out_file.getMutableData() + stream_rotations_count_pos, &rotation_curves_count, sizeof(rotation_curves_count));

		const StaticString<MAX_PATH_LENGTH> anim_path(anim.name, ".ani:", src);
		m_compiler.writeCompiledResource(anim_path, Span(out_file.data(), (i32)out_file.size()));
	}
}

int FBXImporter::getVertexSize(const ImportMesh& mesh) const
{
	static const int POSITION_SIZE = sizeof(float) * 3;
	static const int NORMAL_SIZE = sizeof(u8) * 4;
	static const int TANGENT_SIZE = sizeof(u8) * 4;
	static const int UV_SIZE = sizeof(float) * 2;
	static const int COLOR_SIZE = sizeof(u8) * 4;
	static const int BONE_INDICES_WEIGHTS_SIZE = sizeof(float) * 4 + sizeof(u16) * 4;
	int size = POSITION_SIZE;

	if (mesh.fbx->getGeometry()->getNormals()) size += NORMAL_SIZE;
	if (mesh.fbx->getGeometry()->getUVs()) size += UV_SIZE;
	if (mesh.fbx->getGeometry()->getColors() && m_import_vertex_colors) size += COLOR_SIZE;
	if (hasTangents(*mesh.fbx)) size += TANGENT_SIZE;
	if (mesh.is_skinned) size += BONE_INDICES_WEIGHTS_SIZE;

	return size;
}


void FBXImporter::fillSkinInfo(Array<Skin>& skinning, const ImportMesh& import_mesh) const
{
	const ofbx::Mesh* mesh = import_mesh.fbx;
	const ofbx::Geometry* geom = mesh->getGeometry();
	skinning.resize(geom->getVertexCount());
	memset(&skinning[0], 0, skinning.size() * sizeof(skinning[0]));

	auto* skin = mesh->getGeometry()->getSkin();
	if(!skin) {
		ASSERT(import_mesh.bone_idx >= 0);
		skinning.resize(mesh->getGeometry()->getIndexCount());
		for (Skin& skin : skinning) {
			skin.count = 1;
			skin.weights[0] = 1;
			skin.weights[1] = skin.weights[2] = skin.weights[3] = 0;
			skin.joints[0] = skin.joints[1] = skin.joints[2] = skin.joints[3] = import_mesh.bone_idx;
		}
		return;
	}

	for (int i = 0, c = skin->getClusterCount(); i < c; ++i)
	{
		const ofbx::Cluster* cluster = skin->getCluster(i);
		if (cluster->getIndicesCount() == 0) continue;
		int joint = m_bones.indexOf(cluster->getLink());
		ASSERT(joint >= 0);
		const int* cp_indices = cluster->getIndices();
		const double* weights = cluster->getWeights();
		for (int j = 0; j < cluster->getIndicesCount(); ++j)
		{
			int idx = cp_indices[j];
			float weight = (float)weights[j];
			Skin& s = skinning[idx];
			if (s.count < 4)
			{
				s.weights[s.count] = weight;
				s.joints[s.count] = joint;
				++s.count;
			}
			else
			{
				int min = 0;
				for (int m = 1; m < 4; ++m)
				{
					if (s.weights[m] < s.weights[min]) min = m;
				}

				if (s.weights[min] < weight)
				{
					s.weights[min] = weight;
					s.joints[min] = joint;
				}
			}
		}
	}

	for (Skin& s : skinning)
	{
		float sum = 0;
		for (float w : s.weights) sum += w;
		for (float& w : s.weights) w /= sum;
	}
}


Vec3 FBXImporter::fixOrientation(const Vec3& v) const
{
	switch (m_orientation)
	{
		case Orientation::Y_UP: return Vec3(v.x, v.y, v.z);
		case Orientation::Z_UP: return Vec3(v.x, v.z, -v.y);
		case Orientation::Z_MINUS_UP: return Vec3(v.x, -v.z, v.y);
		case Orientation::X_MINUS_UP: return Vec3(v.y, -v.x, v.z);
		case Orientation::X_UP: return Vec3(-v.y, v.x, v.z);
	}
	ASSERT(false);
	return Vec3(v.x, v.y, v.z);
}


Quat FBXImporter::fixOrientation(const Quat& v) const
{
	switch (m_orientation)
	{
		case Orientation::Y_UP: return Quat(v.x, v.y, v.z, v.w);
		case Orientation::Z_UP: return Quat(v.x, v.z, -v.y, v.w);
		case Orientation::Z_MINUS_UP: return Quat(v.x, -v.z, v.y, v.w);
		case Orientation::X_MINUS_UP: return Quat(v.y, -v.x, v.z, v.w);
		case Orientation::X_UP: return Quat(-v.y, v.x, v.z, v.w);
	}
	ASSERT(false);
	return Quat(v.x, v.y, v.z, v.w);
}

void FBXImporter::writeImpostorVertices(const AABB& aabb)
{
	#pragma pack(1)
		struct Vertex
		{
			Vec3 pos;
			u8 normal[4];
			u8 tangent[4];
			Vec2 uv;
		};
	#pragma pack()

	const Vec3 center = (aabb.max + aabb.min) * 0.5f;

	Vec2 min, max;
	getBBProjection(aabb, Ref(min), Ref(max));

	const Vertex vertices[] = {
		{{center.x + min.x, center.y + min.y, center.z},	{128, 255, 128, 0},	 {255, 128, 128, 0}, {0, 0}},
		{{center.x + min.x, center.y + max.y, center.z},	{128, 255, 128, 0},	 {255, 128, 128, 0}, {0, 1}},
		{{center.x + max.x, center.y + max.y, center.z},	{128, 255, 128, 0},	 {255, 128, 128, 0}, {1, 1}},
		{{center.x + max.x, center.y + min.y, center.z},	{128, 255, 128, 0},	 {255, 128, 128, 0}, {1, 0}}
	};

	const u32 vertex_data_size = sizeof(vertices);
	write(vertex_data_size);
	for (const Vertex& vertex : vertices) {
		write(vertex.pos);
		write(vertex.normal);
		write(vertex.tangent);
		write(vertex.uv);
	}
}


void FBXImporter::writeGeometry(int mesh_idx)
{
	float radius_squared = 0;
	OutputMemoryStream vertices_blob(m_allocator);
	const ImportMesh& import_mesh = m_meshes[mesh_idx];
	
	bool are_indices_16_bit = areIndices16Bit(import_mesh);
	if (are_indices_16_bit)
	{
		int index_size = sizeof(u16);
		write(index_size);
		write(import_mesh.indices.size());
		for (int i : import_mesh.indices)
		{
			ASSERT(i <= (1 << 16));
			u16 index = (u16)i;
			write(index);
		}
	}
	else
	{
		int index_size = sizeof(import_mesh.indices[0]);
		write(index_size);
		write(import_mesh.indices.size());
		write(&import_mesh.indices[0], sizeof(import_mesh.indices[0]) * import_mesh.indices.size());
	}
	radius_squared = maximum(radius_squared, import_mesh.radius_squared);

	write((i32)import_mesh.vertex_data.size());
	write(import_mesh.vertex_data.data(), import_mesh.vertex_data.size());

	write(sqrtf(radius_squared));
	write(import_mesh.aabb);
}


void FBXImporter::writeGeometry(const ImportConfig& cfg)
{
	AABB aabb = {{0, 0, 0}, {0, 0, 0}};
	float radius_squared = 0;
	OutputMemoryStream vertices_blob(m_allocator);
	for (const ImportMesh& import_mesh : m_meshes)
	{
		if (!import_mesh.import) continue;
		bool are_indices_16_bit = areIndices16Bit(import_mesh);
		if (are_indices_16_bit)
		{
			int index_size = sizeof(u16);
			write(index_size);
			write(import_mesh.indices.size());
			for (int i : import_mesh.indices)
			{
				ASSERT(i <= (1 << 16));
				u16 index = (u16)i;
				write(index);
			}
		}
		else
		{
			int index_size = sizeof(import_mesh.indices[0]);
			write(index_size);
			write(import_mesh.indices.size());
			write(&import_mesh.indices[0], sizeof(import_mesh.indices[0]) * import_mesh.indices.size());
		}
		aabb.merge(import_mesh.aabb);
		radius_squared = maximum(radius_squared, import_mesh.radius_squared);
	}

	if (cfg.create_impostor) {
		const int index_size = sizeof(u16);
		write(index_size);
		const u16 indices[] = {0, 1, 2, 0, 2, 3};
		const u32 len = lengthOf(indices);
		write(len);
		write(indices, sizeof(indices));
	}

	for (const ImportMesh& import_mesh : m_meshes)
	{
		if (!import_mesh.import) continue;
		write((i32)import_mesh.vertex_data.size());
		write(import_mesh.vertex_data.data(), import_mesh.vertex_data.size());
	}
	if (cfg.create_impostor) {
		writeImpostorVertices(aabb);
	}

	write(sqrtf(radius_squared));
	write(aabb);
}


void FBXImporter::writeImpostorMesh(const char* dir, const char* model_name)
{
	const i32 attribute_count = 4;
	write(attribute_count);

	write(Mesh::AttributeSemantic::POSITION);
	write(gpu::AttributeType::FLOAT);
	write((u8)3);

	write(Mesh::AttributeSemantic::NORMAL);
	write(gpu::AttributeType::U8);
	write((u8)4);

	write(Mesh::AttributeSemantic::TANGENT);
	write(gpu::AttributeType::U8);
	write((u8)4);

	write(Mesh::AttributeSemantic::TEXCOORD0);
	write(gpu::AttributeType::FLOAT);
	write((u8)2);

	const StaticString<MAX_PATH_LENGTH + 10> material_name(dir, model_name, "_impostor.mat");
	i32 length = stringLength(material_name);
	write(length);
	write(material_name, length);

	const char* mesh_name = "impostor";
	length = stringLength(mesh_name);
	write(length);
	write(mesh_name, length);
}


void FBXImporter::writeMeshes(const char* src, int mesh_idx, const ImportConfig& cfg)
{
	const PathInfo src_info(src);
	i32 mesh_count = 0;
	if (mesh_idx >= 0) {
		mesh_count = 1;
	}
	else {
		for (ImportMesh& mesh : m_meshes)
			if (mesh.import) ++mesh_count;
		if (cfg.create_impostor) ++mesh_count;
	}
	write(mesh_count);
	
	auto writeMesh = [&](const ImportMesh& import_mesh ) {
			
		const ofbx::Mesh& mesh = *import_mesh.fbx;

		i32 attribute_count = getAttributeCount(import_mesh);
		write(attribute_count);

		write(Mesh::AttributeSemantic::POSITION);
		write(gpu::AttributeType::FLOAT);
		write((u8)3);
		const ofbx::Geometry* geom = mesh.getGeometry();
		if (geom->getNormals()) {
			write(Mesh::AttributeSemantic::NORMAL);
			write(gpu::AttributeType::I8);
			write((u8)4);

		}
		if (geom->getUVs()) {
			write(Mesh::AttributeSemantic::TEXCOORD0);
			write(gpu::AttributeType::FLOAT);
			write((u8)2);
		}
		if (geom->getColors() && m_import_vertex_colors) {
			write(Mesh::AttributeSemantic::COLOR0);
			write(gpu::AttributeType::U8);
			write((u8)4);
		}
		if (hasTangents(mesh)) {
			write(Mesh::AttributeSemantic::TANGENT);
			write(gpu::AttributeType::I8);
			write((u8)4);
		}

		if (import_mesh.is_skinned) {
			write(Mesh::AttributeSemantic::INDICES);
			write(gpu::AttributeType::I16);
			write((u8)4);
			write(Mesh::AttributeSemantic::WEIGHTS);
			write(gpu::AttributeType::FLOAT);
			write((u8)4);
		}

		const ofbx::Material* material = import_mesh.fbx_mat;
		char mat[128];
		getMaterialName(material, mat);
		StaticString<MAX_PATH_LENGTH + 128> mat_id(src_info.m_dir, mat, ".mat");
		const i32 len = stringLength(mat_id.data);
		write(len);
		write(mat_id.data, len);

		char name[256];
		getImportMeshName(import_mesh, name);
		i32 name_len = (i32)stringLength(name);
		write(name_len);
		write(name, stringLength(name));
	};

	if(mesh_idx >= 0) {
		writeMesh(m_meshes[mesh_idx]);
	}
	else {
		for (ImportMesh& import_mesh : m_meshes) {
			if (import_mesh.import) writeMesh(import_mesh);
		}
	}

	if (mesh_idx < 0 && cfg.create_impostor) {
		writeImpostorMesh(src_info.m_dir, src_info.m_basename);
	}
}


void FBXImporter::writeSkeleton(const ImportConfig& cfg)
{
	write(m_bones.size());

	for (const ofbx::Object*& node : m_bones)
	{
		const char* name = node->name;
		int len = (int)stringLength(name);
		write(len);
		writeString(name);

		ofbx::Object* parent = node->getParent();
		if (!parent)
		{
			write((int)-1);
		}
		else
		{
			const int idx = m_bones.indexOf(parent);
			write(idx);
		}

		const ImportMesh* mesh = getAnyMeshFromBone(node, int(&node - m_bones.begin()));
		Matrix tr = toLumix(getBindPoseMatrix(mesh, node));
		tr.normalizeScale();

		Quat q = fixOrientation(tr.getRotation());
		Vec3 t = fixOrientation(tr.getTranslation());
		write(t * cfg.mesh_scale * m_fbx_scale);
		write(q);
	}
}


void FBXImporter::writeLODs(const ImportConfig& cfg)
{
	i32 lod_count = 1;
	i32 last_mesh_idx = -1;
	i32 lods[8] = {};
	for (auto& mesh : m_meshes) {
		if (!mesh.import) continue;

		++last_mesh_idx;
		if (mesh.lod >= lengthOf(cfg.lods_distances)) continue;
		lod_count = mesh.lod + 1;
		lods[mesh.lod] = last_mesh_idx;
	}

	for (u32 i = 1; i < Lumix::lengthOf(lods); ++i) {
		if (lods[i] < lods[i - 1]) lods[i] = lods[i - 1];
	}

	if (cfg.create_impostor) {
		lods[lod_count] = last_mesh_idx + 1;
		++lod_count;
	}

	write((const char*)&lod_count, sizeof(lod_count));

	for (int i = 0; i < lod_count; ++i) {
		i32 to_mesh = lods[i];
		write((const char*)&to_mesh, sizeof(to_mesh));
		float factor = cfg.lods_distances[i] < 0 ? FLT_MAX : cfg.lods_distances[i] * cfg.lods_distances[i];
		write((const char*)&factor, sizeof(factor));
	}
}


int FBXImporter::getAttributeCount(const ImportMesh& mesh) const
{
	int count = 1; // position
	const bool has_normals = mesh.fbx->getGeometry()->getNormals();
	const bool has_uvs = mesh.fbx->getGeometry()->getUVs();
	if (has_normals) ++count;
	if (has_uvs) ++count;
	if (mesh.fbx->getGeometry()->getColors() && m_import_vertex_colors) ++count;
	if (hasTangents(*mesh.fbx)) ++count;
	if (mesh.is_skinned) count += 2;
	return count;
}


bool FBXImporter::areIndices16Bit(const ImportMesh& mesh) const
{
	int vertex_size = getVertexSize(mesh);
	return !(mesh.import && mesh.vertex_data.size() / vertex_size > (1 << 16));
}


void FBXImporter::writeModelHeader()
{
	Model::FileHeader header;
	header.magic = 0x5f4c4d4f; // == '_LMO';
	header.version = (u32)Model::FileVersion::LATEST;
	write(header);
}


void FBXImporter::writePhysicsTriMesh(OutputMemoryStream& file)
{
	i32 count = 0;
	for (auto& mesh : m_meshes) {
		count += mesh.indices.size();
	}
	file.write((const char*)&count, sizeof(count));
	int offset = 0;
	for (auto& mesh : m_meshes)
	{
		for (unsigned int j = 0, c = mesh.indices.size(); j < c; ++j)
		{
			u32 index = mesh.indices[j] + offset;
			file.write((const char*)&index, sizeof(index));
		}
		int vertex_size = getVertexSize(mesh);
		int vertex_count = (i32)(mesh.vertex_data.size() / vertex_size);
		offset += vertex_count;
	}
}


void FBXImporter::writePhysics(const char* src, const ImportConfig& cfg)
{
	if (m_meshes.empty()) return;
	if (cfg.physics == ImportConfig::Physics::NONE) return;

	out_file.clear();

	PhysicsGeometry::Header header;
	header.m_magic = PhysicsGeometry::HEADER_MAGIC;
	header.m_version = (u32)PhysicsGeometry::Versions::LAST;
	const bool to_convex = cfg.physics == ImportConfig::Physics::CONVEX;
	header.m_convex = (u32)to_convex;
	out_file.write(&header, sizeof(header));

	i32 count = 0;
	for (auto& mesh : m_meshes)	{
		count += (i32)(mesh.vertex_data.size() / getVertexSize(mesh));
	}

	out_file.write(&count, sizeof(count));
	for (auto& mesh : m_meshes) {
		int vertex_size = getVertexSize(mesh);
		int vertex_count = (i32)(mesh.vertex_data.size() / vertex_size);

		const u8* verts = mesh.vertex_data.data();

		for (int i = 0; i < vertex_count; ++i) {
			out_file.write(verts + i * vertex_size, sizeof(Vec3));
		}
	}

	if (!to_convex) writePhysicsTriMesh(out_file);

	const StaticString<MAX_PATH_LENGTH> phy_path(".phy:", src);
	m_compiler.writeCompiledResource(phy_path, Span(out_file.data(), (i32)out_file.size()));
}


void FBXImporter::writePrefab(const char* src, const ImportConfig& cfg)
{
	Engine& engine = m_app.getWorldEditor().getEngine();
	Universe& universe = engine.createUniverse(false);


	OS::OutputFile file;
	PathInfo file_info(src);
	StaticString<MAX_PATH_LENGTH> tmp(file_info.m_dir, "/", file_info.m_basename, ".fab");
	if (!m_filesystem.open(tmp, Ref(file))) return;

	OutputMemoryStream blob(m_allocator);
	
	const EntityRef root = universe.createEntity({0, 0, 0}, Quat::IDENTITY);

	static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
	for(int i  = 0; i < m_meshes.size(); ++i) {
		const EntityRef e = universe.createEntity({0, 0, 0}, Quat::IDENTITY);
		universe.createComponent(MODEL_INSTANCE_TYPE, e);
		universe.setParent(root, e);
		char mesh_name[256];
		getImportMeshName(m_meshes[i], mesh_name);
		StaticString<MAX_PATH_LENGTH> mesh_path(mesh_name, ".fbx:", src);
		RenderScene* scene = (RenderScene*)universe.getScene(MODEL_INSTANCE_TYPE);
		scene->setModelInstancePath(e, Path(mesh_path));
	}

	engine.serialize(universe, blob);
	engine.destroyUniverse(universe);

	file.write(blob.data(), blob.size());
	file.close();
}


void FBXImporter::writeSubmodels(const char* src, const ImportConfig& cfg)
{
	PROFILE_FUNCTION();
	postprocessMeshes(cfg, src);

	for (int i = 0; i < m_meshes.size(); ++i) {
		char name[256];
		getImportMeshName(m_meshes[i], name);

		out_file.clear();
		writeModelHeader();
		writeMeshes(src, i, cfg);
		writeGeometry(i);
		const ofbx::Skin* skin = m_meshes[i].fbx->getGeometry()->getSkin();
		if (m_meshes[i].is_skinned) {
			writeSkeleton(cfg);
		}
		else {
			write((i32)0);
		}

		// lods
		const i32 lod_count = 1;
		const i32 to_mesh = 0;
		const float factor = FLT_MAX;
		write(lod_count);
		write(to_mesh);
		write(factor);

		StaticString<MAX_PATH_LENGTH> resource_locator(name, ".fbx:", src);

		m_compiler.writeCompiledResource(resource_locator, Span(out_file.data(), (i32)out_file.size()));
	}
}


void FBXImporter::writeModel(const char* src, const ImportConfig& cfg)
{
	PROFILE_FUNCTION();
	postprocessMeshes(cfg, src);

	auto cmpMeshes = [](const void* a, const void* b) -> int {
		auto a_mesh = static_cast<const ImportMesh*>(a);
		auto b_mesh = static_cast<const ImportMesh*>(b);
		return a_mesh->lod - b_mesh->lod;
	};

	bool import_any_mesh = false;
	for (const ImportMesh& m : m_meshes) {
		if (m.import) import_any_mesh = true;
	}
	if (!import_any_mesh) return;

	qsort(&m_meshes[0], m_meshes.size(), sizeof(m_meshes[0]), cmpMeshes);
	out_file.clear();
	writeModelHeader();
	writeMeshes(src, -1, cfg);
	writeGeometry(cfg);
	writeSkeleton(cfg);
	writeLODs(cfg);

	m_compiler.writeCompiledResource(src, Span(out_file.data(), (i32)out_file.size()));
}


} // namespace Lumix