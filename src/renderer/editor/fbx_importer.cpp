#include "fbx_importer.h"
#include "animation/animation.h"
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "core/atomic.h"
#include "core/crt.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "core/hash.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/path.h"
#include "core/stack_array.h"
#include "engine/prefab.h"
#include "core/profiler.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "meshoptimizer/meshoptimizer.h"
#include "mikktspace/mikktspace.h"
#include "openfbx/ofbx.h"
#include "physics/physics_resources.h"
#include "physics/physics_module.h"
#include "physics/physics_system.h"
#include "renderer/draw_stream.h"
#include "renderer/editor/model_importer.h"
#include "renderer/editor/model_meta.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"

namespace Lumix {

namespace {

struct FBXImportGeometry {
	const ofbx::GeometryData* geom;
	const ofbx::Mesh* mesh;
	i32 bone_idx = -1;
};

struct GeomPartition {
	const ofbx::GeometryData* geom;
	u32 partition;
	u32 material;
	bool flip_handness;

	bool operator==(const GeomPartition& rhs) const { 
		return geom == rhs.geom 
			&& partition == rhs.partition 
			&& material == rhs.material
			&& flip_handness == rhs.flip_handness;
	}
};

}

template<> struct HashFunc<GeomPartition> {
	static u32 get(const GeomPartition& k) {
		return k.partition ^ k.material ^ u32(u64(k.geom)) ^ u32(u64(k.geom) >> 32);
	}
};

struct FBXImporter : ModelImporter {
	struct Skin {
		float weights[4];
		i16 joints[4];
		int count = 0;
	};

	struct VertexLayout {
		i32 size = -1;
		i32 normal_offset = -1;
		i32 uv_offset = -1;
		i32 tangent_offset = -1;
	};

	enum class Orientation {
		Y_UP,
		Z_UP,
		Z_MINUS_UP,
		X_MINUS_UP,
		X_UP
	};

	FBXImporter(StudioApp& app, IAllocator& allocator)
		: ModelImporter(app)
		, m_allocator(allocator)
		, m_scene(nullptr)
		, m_fbx_meshes(m_allocator)
	{}

	~FBXImporter()
	{
		if (m_scene) m_scene->destroy();
		if (m_impostor_shadow_shader) m_impostor_shadow_shader->decRefCount();
	}

	static StringView toStringView(ofbx::DataView data) {
		return StringView(
			(const char*)data.begin,
			(const char*)data.end
		);
	}

	static bool isConstCurve(const ofbx::AnimationCurve* curve) {
		if (!curve) return true;
		if (curve->getKeyCount() <= 1) return true;
		const float* values = curve->getKeyValue();
		if (curve->getKeyCount() == 2 && fabsf(values[1] - values[0]) < 1e-6) return true;
		return false;
	}

	static Vec3 toLumixVec3(const ofbx::DVec3& v) { return {(float)v.x, (float)v.y, (float)v.z}; }
	static Vec3 toLumixVec3(const ofbx::FVec3& v) { return {(float)v.x, (float)v.y, (float)v.z}; }

	static Matrix toLumix(const ofbx::DMatrix& mtx) {
		Matrix res;
		for (int i = 0; i < 16; ++i) (&res.columns[0].x)[i] = (float)mtx.m[i];
		return res;
	}

	static u32 packColor(const ofbx::Vec4& vec) {
		const i8 xx = i8(clamp((vec.x * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);
		const i8 yy = i8(clamp((vec.y * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);
		const i8 zz = i8(clamp((vec.z * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);
		const i8 ww = i8(clamp((vec.w * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);

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

	static bool doesFlipHandness(const Matrix& mtx) {
		Vec3 x(1, 0, 0);
		Vec3 y(0, 1, 0);
		Vec3 z = mtx.inverted().transformVector(cross(mtx.transformVector(x), mtx.transformVector(y)));
		return z.z < 0;
	}

	static bool hasTangents(const ofbx::GeometryData& geom) {
		if (geom.getTangents().values) return true;
		if (geom.getUVs().values) return true;
		return false;
	}

	static int getVertexSize(const ofbx::GeometryData& geom, bool is_skinned, const ModelMeta& meta) {
		static const int POSITION_SIZE = sizeof(float) * 3;
		static const int NORMAL_SIZE = sizeof(u8) * 4;
		static const int TANGENT_SIZE = sizeof(u8) * 4;
		static const int UV_SIZE = sizeof(float) * 2;
		static const int COLOR_SIZE = sizeof(u8) * 4;
		static const int AO_SIZE = sizeof(u8) * 4;
		static const int BONE_INDICES_WEIGHTS_SIZE = sizeof(float) * 4 + sizeof(u16) * 4;
		int size = POSITION_SIZE + NORMAL_SIZE;

		if (geom.getUVs().values) size += UV_SIZE;
		if (meta.bake_vertex_ao) size += AO_SIZE;
		if (geom.getColors().values && meta.import_vertex_colors) size += meta.vertex_color_is_ao ? AO_SIZE : COLOR_SIZE;
		if (hasTangents(geom)) size += TANGENT_SIZE;
		if (is_skinned) size += BONE_INDICES_WEIGHTS_SIZE;

		return size;
	}

	static bool areIndices16Bit(const ImportGeometry& mesh) {
		int vertex_size = mesh.vertex_size;
		return mesh.vertex_buffer.size() / vertex_size < (1 << 16);
	}

	// flat shading
	static void computeNormals(OutputMemoryStream& unindexed_triangles, const VertexLayout& layout) {
		PROFILE_FUNCTION();
		const u32 vertex_size = layout.size;
		const int vertex_count = int(unindexed_triangles.size() / vertex_size);

		const u8* positions = unindexed_triangles.getMutableData();
		u8* normals = unindexed_triangles.getMutableData() + layout.normal_offset;

		for (int i = 0; i < vertex_count; i += 3) {
			Vec3 v0; memcpy(&v0, positions + i * vertex_size, sizeof(v0));
			Vec3 v1; memcpy(&v1, positions + (i + 1) * vertex_size, sizeof(v1));
			Vec3 v2; memcpy(&v2, positions + (i + 2) * vertex_size, sizeof(v2));
			Vec3 n = normalize(cross(v1 - v0, v2 - v0));
			u32 npacked = packF4u(n);

			memcpy(normals + i * vertex_size, &npacked, sizeof(npacked));
			memcpy(normals + (i + 1) * vertex_size, &npacked, sizeof(npacked));
			memcpy(normals + (i + 2) * vertex_size, &npacked, sizeof(npacked));
		}
	}

	static void computeTangents(OutputMemoryStream& unindexed_triangles, const VertexLayout& layout, const Path& path) {
		PROFILE_FUNCTION();

		struct {
			OutputMemoryStream* out;
			int num_triangles;
			int vertex_size;
			const u8* positions;
			const u8* normals;
			const u8* uvs;
			u8* tangents;
		} data;
		data.out = &unindexed_triangles;
		data.num_triangles = int(unindexed_triangles.size() / layout.size / 3);
		data.vertex_size = layout.size;
		data.positions = unindexed_triangles.data();
		data.normals = unindexed_triangles.data() + layout.normal_offset;
		data.tangents = unindexed_triangles.getMutableData() + layout.tangent_offset;
		data.uvs = unindexed_triangles.data() + layout.uv_offset;

		SMikkTSpaceInterface iface = {};
		iface.m_getNumFaces = [](const SMikkTSpaceContext * pContext) -> int {
			auto* ptr = (decltype(data)*)pContext->m_pUserData;
			return ptr->num_triangles;
		};
		iface.m_getNumVerticesOfFace = [](const SMikkTSpaceContext * pContext, const int face) -> int { return 3; };
		iface.m_getPosition = [](const SMikkTSpaceContext * pContext, float fvPosOut[], const int iFace, const int iVert) { 
			auto* ptr = (decltype(data)*)pContext->m_pUserData;
			const u8* data = ptr->positions + ptr->vertex_size * (iFace * 3 + iVert);
			Vec3 p;
			memcpy(&p, data, sizeof(p));
			fvPosOut[0] = p.x;
			fvPosOut[1] = p.y;
			fvPosOut[2] = p.z;
		};
		iface.m_getNormal = [](const SMikkTSpaceContext * pContext, float fvNormOut[], const int iFace, const int iVert) { 
			auto* ptr = (decltype(data)*)pContext->m_pUserData;
			const u8* data = ptr->normals + ptr->vertex_size * (iFace * 3 + iVert);
			u32 packed;
			memcpy(&packed, data, sizeof(packed));
			const Vec3 normal = unpackF4u(packed);
			fvNormOut[0] = normal.x;
			fvNormOut[1] = normal.y;
			fvNormOut[2] = normal.z;
		};
		iface.m_getTexCoord = [](const SMikkTSpaceContext * pContext, float fvTexcOut[], const int iFace, const int iVert) { 
			auto* ptr = (decltype(data)*)pContext->m_pUserData;
			const u8* data = ptr->uvs + ptr->vertex_size * (iFace * 3 + iVert);
			Vec2 p;
			memcpy(&p, data, sizeof(p));
			fvTexcOut[0] = p.x;
			fvTexcOut[1] = p.y;
		};
		iface.m_setTSpaceBasic  = [](const SMikkTSpaceContext * pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert) {
			auto* ptr = (decltype(data)*)pContext->m_pUserData;
			u8* data = ptr->tangents + ptr->vertex_size * (iFace * 3 + iVert);
			Vec3 t;
			t.x = fvTangent[0];
			t.y = fvTangent[1];
			t.z = fvTangent[2];
			u32 packed = packF4u(t);
			memcpy(data, &packed, sizeof(packed));
		};

		SMikkTSpaceContext ctx;
		ctx.m_pUserData = &data;
		ctx.m_pInterface = &iface;
		tbool res = genTangSpaceDefault(&ctx);
		if (!res) {
			logError(path, ": failed to generate tangent space");
		}
	}

	static void computeTangentsSimple(OutputMemoryStream& unindexed_triangles, const VertexLayout& layout) {
		PROFILE_FUNCTION();
		const u32 vertex_size = layout.size;
		const int vertex_count = int(unindexed_triangles.size() / vertex_size);

		const u8* positions = unindexed_triangles.data();
		const u8* uvs = unindexed_triangles.data() + layout.uv_offset;
		u8* tangents = unindexed_triangles.getMutableData() + layout.tangent_offset;

		for (int i = 0; i < vertex_count; i += 3) {
			Vec3 v0; memcpy(&v0, positions + i * vertex_size, sizeof(v0));
			Vec3 v1; memcpy(&v1, positions + (i + 1) * vertex_size, sizeof(v1));
			Vec3 v2; memcpy(&v2, positions + (i + 2) * vertex_size, sizeof(v2));
			Vec2 uv0; memcpy(&uv0, uvs + i * vertex_size, sizeof(uv0));
			Vec2 uv1; memcpy(&uv1, uvs + (i + 1) * vertex_size, sizeof(uv1));
			Vec2 uv2; memcpy(&uv2, uvs + (i + 2) * vertex_size, sizeof(uv2));

			const Vec3 dv10 = v1 - v0;
			const Vec3 dv20 = v2 - v0;
			const Vec2 duv10 = uv1 - uv0;
			const Vec2 duv20 = uv2 - uv0;

			const float dir = duv20.x * duv10.y - duv20.y * duv10.x < 0 ? -1.f : 1.f;
			Vec3 tangent; 
			tangent.x = (dv20.x * duv10.y - dv10.x * duv20.y) * dir;
			tangent.y = (dv20.y * duv10.y - dv10.y * duv20.y) * dir;
			tangent.z = (dv20.z * duv10.y - dv10.z * duv20.y) * dir;
			const float l = 1 / sqrtf(float(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z));
			tangent.x *= l;
			tangent.y *= l;
			tangent.z *= l;
			
			u32 tangent_packed = packF4u(tangent);

			memcpy(tangents + i * vertex_size, &tangent_packed, sizeof(tangent_packed));
			memcpy(tangents + (i + 1) * vertex_size, &tangent_packed, sizeof(tangent_packed));
			memcpy(tangents + (i + 2) * vertex_size, &tangent_packed, sizeof(tangent_packed));
		}
	}

	static void remap(const OutputMemoryStream& unindexed_triangles, ImportGeometry& mesh) {
		PROFILE_FUNCTION();
		const u32 vertex_count = u32(unindexed_triangles.size() / mesh.vertex_size);
		mesh.indices.resize(vertex_count);

		u32 unique_vertex_count = (u32)meshopt_generateVertexRemap(mesh.indices.begin(), nullptr, vertex_count, unindexed_triangles.data(), vertex_count, mesh.vertex_size);

		mesh.vertex_buffer.resize(unique_vertex_count * mesh.vertex_size);

		u8* vb = mesh.vertex_buffer.getMutableData();
		for (u32 i = 0; i < vertex_count; ++i) {
			const u8* src = unindexed_triangles.data() + i * mesh.vertex_size;
			u8* dst = vb + mesh.indices[i] * mesh.vertex_size;
			memcpy(dst, src, mesh.vertex_size);
		}
	}

	// convert from ofbx to runtime vertex data, compute missing info (normals, tangents, ao, ...)
	void postprocess(const ModelMeta& meta, const Path& path) {
		AtomicI32 geom_idx_getter = 0;
		jobs::runOnWorkers([&](){
			Array<Skin> skinning(m_allocator);
			OutputMemoryStream unindexed_triangles(m_allocator);
			Array<i32> tri_indices_tmp(m_allocator);

			for (;;) {
				i32 geom_idx = geom_idx_getter.inc();
				if (geom_idx >= m_geometries.size()) break;
				
				ImportGeometry& import_geom = m_geometries[geom_idx];
				const FBXImportGeometry* fbx_geom = (const FBXImportGeometry*)import_geom.user_data;

				const ofbx::GeometryData& geom = *fbx_geom->geom;
				import_geom.vertex_size = getVertexSize(geom, import_geom.is_skinned, meta);
				ofbx::GeometryPartition partition = geom.getPartition(import_geom.submesh == -1 ? 0 : import_geom.submesh);
				if (partition.polygon_count == 0) continue;

				import_geom.attributes.push({
					.semantic = AttributeSemantic::POSITION,
					.type = gpu::AttributeType::FLOAT,
					.num_components = 3
				});

				import_geom.attributes.push({
					.semantic = AttributeSemantic::NORMAL,
					.type = gpu::AttributeType::I8,
					.num_components = 4
				});

				if (geom.getUVs().values) {
					import_geom.attributes.push({
						.semantic = AttributeSemantic::TEXCOORD0,
						.type = gpu::AttributeType::FLOAT,
						.num_components = 2
					});
				}

				if (meta.bake_vertex_ao) {
					import_geom.attributes.push({
						.semantic = AttributeSemantic::AO,
						.type = gpu::AttributeType::U8,
						.num_components = 4 // 1+3 because of padding
					});
				}
				if (geom.getColors().values && meta.import_vertex_colors) {
					if (meta.vertex_color_is_ao) {
						import_geom.attributes.push({
							.semantic = AttributeSemantic::AO,
							.type = gpu::AttributeType::U8,
							.num_components = 4 // 1+3 because of padding
						});
					}
					else {
						import_geom.attributes.push({
							.semantic = AttributeSemantic::COLOR0,
							.type = gpu::AttributeType::U8,
							.num_components = 4
						});
					}
				}

				if (hasTangents(geom)) {
					import_geom.attributes.push({
						.semantic = AttributeSemantic::TANGENT,
						.type = gpu::AttributeType::I8,
						.num_components = 4
					});
				}

				if (import_geom.is_skinned) {
					import_geom.attributes.push({
						.semantic = AttributeSemantic::JOINTS,
						.type = gpu::AttributeType::U16,
						.num_components = 4
					});
					import_geom.attributes.push({
						.semantic = AttributeSemantic::WEIGHTS,
						.type = gpu::AttributeType::FLOAT,
						.num_components = 4
					});
				}

				PROFILE_BLOCK("FBX convert vertex data")
				profiler::pushInt("Triangle count", partition.triangles_count);

				ofbx::Vec3Attributes normals = geom.getNormals();
				ofbx::Vec3Attributes tangents = geom.getTangents();
				ofbx::Vec4Attributes colors = meta.import_vertex_colors ? geom.getColors() : ofbx::Vec4Attributes{};
				ofbx::Vec2Attributes uvs = geom.getUVs();

				VertexLayout vertex_layout;

				const bool compute_tangents = !tangents.values && uvs.values || meta.force_recompute_tangents;
			
				vertex_layout.size = sizeof(Vec3); // position
				vertex_layout.normal_offset = vertex_layout.size;
				vertex_layout.size += sizeof(u32); // normals
				vertex_layout.uv_offset = vertex_layout.size;
				if (uvs.values) vertex_layout.size += sizeof(Vec2);
				if (meta.bake_vertex_ao) vertex_layout.size += sizeof(u32);
				if (colors.values && meta.import_vertex_colors) vertex_layout.size += sizeof(u32);
				vertex_layout.tangent_offset = vertex_layout.size;
				if (tangents.values || compute_tangents) vertex_layout.size += sizeof(u32);
				if (import_geom.is_skinned) vertex_layout.size += sizeof(Vec4) + 4 * sizeof(u16);

				if (import_geom.is_skinned) {
					const FBXImportGeometry* g = (const FBXImportGeometry*)import_geom.user_data;
					fillSkinInfo(skinning, g->bone_idx, fbx_geom->mesh);
					triangulate(unindexed_triangles, import_geom, partition, geom, &skinning, meta, tri_indices_tmp);
				}
				else {
					triangulate(unindexed_triangles, import_geom, partition, geom, nullptr, meta, tri_indices_tmp);
				}

				if (!normals.values || meta.force_recompute_normals) computeNormals(unindexed_triangles, vertex_layout);

				if (compute_tangents) {
					if (meta.use_mikktspace) {
						computeTangents(unindexed_triangles, vertex_layout, path);
					}
					else {
						computeTangentsSimple(unindexed_triangles, vertex_layout);
					}
				}

				remap(unindexed_triangles, import_geom);
				import_geom.index_size = areIndices16Bit(import_geom) ? 2 : 4;
				
				if (import_geom.flip_handness) {
					const u32 num_vertices = u32(import_geom.vertex_buffer.size() / import_geom.vertex_size);
					u8* data = import_geom.vertex_buffer.getMutableData();
					auto transform_vec = [&](u32 offset){
						u32 packed;
						memcpy(&packed, data + offset, sizeof(packed));
						Vec3 v = unpackF4u(packed);
						v.x *= -1;
						packed = packF4u(v);
						memcpy(data + offset, &packed, sizeof(packed));
					};
					for (u32 i = 0; i < num_vertices; ++i) {
						Vec3 p;
						memcpy(&p, data + i * import_geom.vertex_size, sizeof(p));
						p.x *= -1;
						memcpy(data + i * import_geom.vertex_size, &p, sizeof(p));
						transform_vec(i * import_geom.vertex_size + vertex_layout.normal_offset);
						transform_vec(i * import_geom.vertex_size + vertex_layout.tangent_offset);
					}

					for (i32 i = 0, n = import_geom.indices.size(); i < n; i += 3) {
						swap(import_geom.indices[i], import_geom.indices[i + 1]);
					}
				}
			}
		});

		postprocessCommon(meta);
}

	void insertHierarchy(const ofbx::Object* node) {
		if (!node) return;
		if (m_bones.find([&](const Bone& bone){ return bone.id == u64(node); }) >= 0) return;

		ofbx::Object* parent = node->getParent();
		insertHierarchy(parent);
		Bone& bone = m_bones.emplace(m_allocator);
		bone.id = u64(node);
	}


	ofbx::DMatrix getBindPoseMatrix(const ofbx::Mesh* mesh, const ofbx::Object* node) const {
		if (!mesh) return node->getGlobalTransform();

		auto* skin = mesh->getSkin();
		if (!skin) return node->getGlobalTransform();

		for (int i = 0, c = skin->getClusterCount(); i < c; ++i) {
			const ofbx::Cluster* cluster = skin->getCluster(i);
			if (cluster->getLink() == node) {
				return cluster->getTransformLinkMatrix();
			}
		}
		return node->getGlobalTransform();
	}

	void gatherBones(bool force_skinned) {
		PROFILE_FUNCTION();
		for (const ImportMesh& mesh : m_meshes) {
			const ofbx::Mesh* fbx_mesh = m_fbx_meshes[mesh.mesh_index];
			const ofbx::Skin* skin = fbx_mesh->getSkin();
			if (skin) {
				for (int i = 0; i < skin->getClusterCount(); ++i) {
					const ofbx::Cluster* cluster = skin->getCluster(i);
					insertHierarchy(cluster->getLink());
				}
			}

			if (force_skinned) {
				insertHierarchy(fbx_mesh);
			}
		}

		for (int i = 0, n = m_scene->getAnimationStackCount(); i < n; ++i) {
			const ofbx::AnimationStack* stack = m_scene->getAnimationStack(i);
			for (int j = 0; stack->getLayer(j); ++j) {
				const ofbx::AnimationLayer* layer = stack->getLayer(j);
				for (int k = 0; layer->getCurveNode(k); ++k) {
					const ofbx::AnimationCurveNode* node = layer->getCurveNode(k);
					if (node->getBone()) insertHierarchy(node->getBone());
				}
			}
		}

		m_bones.removeDuplicates();
		for (Bone& bone : m_bones) {
			const ofbx::Object* node = (const ofbx::Object*)bone.id;
			bone.parent_id = node->getParent() ? u64(node->getParent()) : 0;
		}

		sortBones();

		if (force_skinned) {
			for (ImportGeometry& g : m_geometries) {
				FBXImportGeometry* fbx_geom = (FBXImportGeometry*)g.user_data;
				fbx_geom->bone_idx = m_bones.find([&](const Bone& bone){ return bone.id == u64(fbx_geom->mesh); });
			}
		}

		for (Bone& bone : m_bones) {
			const ofbx::Object* node = (const ofbx::Object*)bone.id;
			const ofbx::Mesh* mesh = getAnyMeshFromBone(node, i32(&bone - m_bones.begin()));
			Matrix tr = toLumix(getBindPoseMatrix(mesh, node));
			tr.normalizeScale(); // TODO why?
			tr.setTranslation(tr.getTranslation() * m_scene_scale);
			bone.bind_pose_matrix = fixOrientation(tr);
			bone.name = node->name;
		}
	}
	
	LUMIX_FORCE_INLINE Quat fixOrientation(const Quat& v) const {
		switch (m_orientation) {
			case Orientation::Y_UP: return v;
			case Orientation::Z_UP: return Quat(v.x, v.z, -v.y, v.w);
			case Orientation::Z_MINUS_UP: return Quat(v.x, -v.z, v.y, v.w);
			case Orientation::X_MINUS_UP: return Quat(v.y, -v.x, v.z, v.w);
			case Orientation::X_UP: return Quat(-v.y, v.x, v.z, v.w);
		}
		ASSERT(false);
		return Quat(v.x, v.y, v.z, v.w);
	}

	LUMIX_FORCE_INLINE Vec3 fixOrientation(const Vec3& v) const {
		switch (m_orientation) {
			case Orientation::Y_UP: return v;
			case Orientation::Z_UP: return Vec3(v.x, v.z, -v.y);
			case Orientation::Z_MINUS_UP: return Vec3(v.x, -v.z, v.y);
			case Orientation::X_MINUS_UP: return Vec3(v.y, -v.x, v.z);
			case Orientation::X_UP: return Vec3(-v.y, v.x, v.z);
		}
		ASSERT(false);
		return v;
	}

	LUMIX_FORCE_INLINE Matrix fixOrientation(const Matrix& m) const {
		switch (m_orientation) {
			case Orientation::Y_UP: return m;
			case Orientation::Z_UP:{
				Matrix mtx = Matrix(
					Vec4(1, 0, 0, 0),
					Vec4(0, 0, -1, 0),
					Vec4(0, 1, 0, 0),
					Vec4(0, 0, 0, 1)
				);
				return mtx * m;
			}
			case Orientation::Z_MINUS_UP:
			case Orientation::X_MINUS_UP:
			case Orientation::X_UP:
				ASSERT(false); // TODO
				break;
		}
		ASSERT(false);
		return m;
	}

	LUMIX_FORCE_INLINE u32 getPackedVec3(ofbx::Vec3 vec) const {
		Vec3 v = toLumixVec3(vec);
		return packF4u(v);
	}

	void fillSkinInfo(Array<Skin>& skinning, i32 force_bone_idx, const ofbx::Mesh* mesh) const {
		const ofbx::Skin* fbx_skin = mesh->getSkin();
		const ofbx::GeometryData& geom = mesh->getGeometryData();
		skinning.resize(geom.getPositions().values_count);
		memset(&skinning[0], 0, skinning.size() * sizeof(skinning[0]));

		if (!fbx_skin) {
			ASSERT(force_bone_idx >= 0);
			for (Skin& skin : skinning) {
				skin.count = 1;
				skin.weights[0] = 1;
				skin.weights[1] = skin.weights[2] = skin.weights[3] = 0;
				skin.joints[0] = skin.joints[1] = skin.joints[2] = skin.joints[3] = force_bone_idx;
			}
			return;
		}

		for (int i = 0, c = fbx_skin->getClusterCount(); i < c; ++i) {
			const ofbx::Cluster* cluster = fbx_skin->getCluster(i);
			if (cluster->getIndicesCount() == 0) continue;
			if (!cluster->getLink()) continue;

			i32 joint = m_bones.find([&](const Bone& bone){ return bone.id == u64(cluster->getLink()); });
			ASSERT(joint >= 0);
			const int* cp_indices = cluster->getIndices();
			const double* weights = cluster->getWeights();
			for (int j = 0; j < cluster->getIndicesCount(); ++j) {
				int idx = cp_indices[j];
				float weight = (float)weights[j];
				Skin& s = skinning[idx];
				if (s.count < 4) {
					s.weights[s.count] = weight;
					s.joints[s.count] = joint;
					++s.count;
				}
				else {
					int min = 0;
					for (int m = 1; m < 4; ++m) {
						if (s.weights[m] < s.weights[min]) min = m;
					}

					if (s.weights[min] < weight) {
						s.weights[min] = weight;
						s.joints[min] = joint;
					}
				}
			}
		}

		for (Skin& s : skinning) {
			float sum = 0;
			for (float w : s.weights) sum += w;
			if (sum == 0) {
				s.weights[0] = 1;
				s.weights[1] = s.weights[2] = s.weights[3] = 0;
				s.joints[0] = s.joints[1] = s.joints[2] = s.joints[3] = 0;
			}
			else {
				for (float& w : s.weights) w /= sum;
			}
		}
	}

	void triangulate(OutputMemoryStream& unindexed_triangles
		, ImportGeometry& mesh
		, const ofbx::GeometryPartition& partition
		, const ofbx::GeometryData& geom
		, const Array<Skin>* skinning
		, const ModelMeta& meta
		, Array<i32>& tri_indices) const
	{
		PROFILE_FUNCTION();
		ofbx::Vec3Attributes positions = geom.getPositions();
		ofbx::Vec3Attributes normals = geom.getNormals();
		ofbx::Vec3Attributes tangents = geom.getTangents();
		ofbx::Vec4Attributes colors = meta.import_vertex_colors ? geom.getColors() : ofbx::Vec4Attributes{};
		ofbx::Vec2Attributes uvs = geom.getUVs();
		const bool compute_tangents = !tangents.values && uvs.values;

		tri_indices.resize(partition.max_polygon_triangles * 3);
		unindexed_triangles.clear();
		unindexed_triangles.resize(mesh.vertex_size * 3 * partition.triangles_count);
		u8* dst = unindexed_triangles.getMutableData();
		// convert to interleaved vertex data of unindexed triangles
		//	tri[0].v[0].pos, tri[0].v[0].normal, ... tri[0].v[2].tangent, tri[1].v[0].pos, ...
		auto write = [&](const auto& v){
			memcpy(dst, &v, sizeof(v));
			dst += sizeof(v);
		};
		
		for (i32 polygon_idx = 0; polygon_idx < partition.polygon_count; ++polygon_idx) {
			const ofbx::GeometryPartition::Polygon& polygon = partition.polygons[polygon_idx];
			u32 tri_count = ofbx::triangulate(geom, polygon, tri_indices.begin());
			for (u32 i = 0; i < tri_count; ++i) {
				ofbx::Vec3 cp = positions.get(tri_indices[i]);
				write(toLumixVec3(cp));
		
				if (normals.values) write(getPackedVec3(normals.get(tri_indices[i])));
				else write(u32(0));
				if (uvs.values) {
					ofbx::Vec2 uv = uvs.get(tri_indices[i]);
					write(Vec2(uv.x, 1 - uv.y));
				}
				if (meta.bake_vertex_ao) write(u32(0));
				if (colors.values && meta.import_vertex_colors) {
					ofbx::Vec4 color = colors.get(tri_indices[i]);
					if (meta.vertex_color_is_ao) {
						const u8 ao[4] = { u8(color.x * 255.f + 0.5f) };
						memcpy(dst, ao, sizeof(ao));
						dst += sizeof(ao);
					} else {
						write(packColor(color));
					}
				}
				if (tangents.values) write(getPackedVec3(tangents.get(tri_indices[i])));
				else if (compute_tangents) write(u32(0));
				if (skinning) {
					if (positions.indices) {
						write((*skinning)[positions.indices[tri_indices[i]]].joints);
						write((*skinning)[positions.indices[tri_indices[i]]].weights);
					}
					else {
						write((*skinning)[tri_indices[i]].joints);
						write((*skinning)[tri_indices[i]].weights);
					}
				}
			}
		}
	}

	void sortBones() {
		const int count = m_bones.size();
		u32 first_nonroot = 0;
		for (i32 i = 0; i < count; ++i) {
			if (m_bones[i].parent_id == 0) {
				swap(m_bones[i], m_bones[first_nonroot]);
				++first_nonroot;
			}
		}

		for (i32 i = 0; i < count; ++i) {
			for (int j = i + 1; j < count; ++j) {
				if (m_bones[i].parent_id == m_bones[j].id) {
					Bone bone = static_cast<Bone&&>(m_bones[j]);
					m_bones.swapAndPop(j);
					m_bones.insert(i, static_cast<Bone&&>(bone));
					--i;
					break;
				}
			}
		}
	}

	const ofbx::Mesh* getAnyMeshFromBone(const ofbx::Object* node, i32 bone_idx) const {
		for (const ImportGeometry& geom : m_geometries) {
			FBXImportGeometry* fbx_geom = (FBXImportGeometry*)geom.user_data;
			if (fbx_geom->bone_idx == bone_idx) {
				return fbx_geom->mesh;
			}

			auto* skin = fbx_geom->mesh->getSkin();
			if (!skin) continue;

			for (int j = 0, c = skin->getClusterCount(); j < c; ++j) {
				if (skin->getCluster(j)->getLink() == node) return fbx_geom->mesh;
			}
		}
		return nullptr;
	}

	void gatherAnimations(StringView src) {
		PROFILE_FUNCTION();
		int anim_count = m_scene->getAnimationStackCount();
		for (int i = 0; i < anim_count; ++i) {
			ImportAnimation& anim = m_animations.emplace();
			anim.index = m_animations.size() - 1;
			const ofbx::AnimationStack* fbx_anim = (const ofbx::AnimationStack*)m_scene->getAnimationStack(i);
			{
				const ofbx::TakeInfo* take_info = m_scene->getTakeInfo(fbx_anim->name);
				if (take_info) {
					if (take_info->name.begin != take_info->name.end) {
						anim.name = toStringView(take_info->name);
					}
					if (anim.name.empty() && take_info->filename.begin != take_info->filename.end) {
						StringView tmp = toStringView(take_info->filename);
						anim.name = Path::getBasename(tmp);
					}
					if (anim.name.empty()) anim.name = "anim";
				}
				else {
					anim.name = "";
				}
			}

			const ofbx::AnimationLayer* anim_layer = fbx_anim->getLayer(0);
			{
				anim.fps = m_scene->getSceneFrameRate();
				const ofbx::TakeInfo* take_info = m_scene->getTakeInfo(fbx_anim->name);
				if(!take_info && startsWith(fbx_anim->name, "AnimStack::")) {
					take_info = m_scene->getTakeInfo(fbx_anim->name + 11);
				}

				if (take_info) {
					anim.length = take_info->local_time_to - take_info->local_time_from;
				}
				else if(m_scene->getGlobalSettings()) {
					anim.length = m_scene->getGlobalSettings()->TimeSpanStop;
				}
				else {
					logError("Unsupported animation in ", src);
					continue;
				}
			}


			if (!anim_layer || !anim_layer->getCurveNode(0)) {
				m_animations.pop();
				continue;
			}

			bool data_found = false;
			for (int k = 0; anim_layer->getCurveNode(k); ++k) {
				const ofbx::AnimationCurveNode* node = anim_layer->getCurveNode(k);
				if (node->getBoneLinkProperty() == "Lcl Translation" || node->getBoneLinkProperty() == "Lcl Rotation") {
					if (!isConstCurve(node->getCurve(0)) || !isConstCurve(node->getCurve(1)) || !isConstCurve(node->getCurve(2))) {
						data_found = true;
						break;
					}
				}
			}
			if (!data_found) m_animations.pop();
		}

		if (m_animations.size() == 1) {
			m_animations[0].name = "";
		}
	}

	static i64 sampleToFBXTime(u32 sample, float fps) {
		return ofbx::secondsToFbxTime(sample / fps);
	}

	static void convert(const ofbx::DMatrix& mtx, Vec3& pos, Quat& rot) {
		Matrix m = toLumix(mtx);
		m.normalizeScale();
		rot = m.getRotation();
		pos = m.getTranslation();
	}

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

	static void fill(const ofbx::Object& bone, const ofbx::AnimationLayer& layer, Array<Key>& keys, u32 from_sample, u32 samples_count, float fps) {
		const ofbx::AnimationCurveNode* translation_node = layer.getCurveNode(bone, "Lcl Translation");
		const ofbx::AnimationCurveNode* rotation_node = layer.getCurveNode(bone, "Lcl Rotation");
		if (!translation_node && !rotation_node) return;

		keys.resize(samples_count);
		
		auto fill_rot = [&](u32 idx, const ofbx::AnimationCurve* curve) {
			if (!curve) {
				const ofbx::DVec3 lcl_rot = bone.getLocalRotation();
				for (Key& k : keys) {
					(&k.rot.x)[idx] = float((&lcl_rot.x)[idx]);
				}
				return;
			}

			for (u32 f = 0; f < samples_count; ++f) {
				Key& k = keys[f];
				(&k.rot.x)[idx] = evalCurve(sampleToFBXTime(from_sample + f, fps), *curve);
			}
		};
		
		auto fill_pos = [&](u32 idx, const ofbx::AnimationCurve* curve) {
			if (!curve) {
				const ofbx::DVec3 lcl_pos = bone.getLocalTranslation();
				for (Key& k : keys) {
					(&k.pos.x)[idx] = float((&lcl_pos.x)[idx]);
				}
				return;
			}

			for (u32 f = 0; f < samples_count; ++f) {
				Key& k = keys[f];
				(&k.pos.x)[idx] = evalCurve(sampleToFBXTime(from_sample + f, fps), *curve);
			}
		};
		
		fill_rot(0, rotation_node ? rotation_node->getCurve(0) : nullptr);
		fill_rot(1, rotation_node ? rotation_node->getCurve(1) : nullptr);
		fill_rot(2, rotation_node ? rotation_node->getCurve(2) : nullptr);

		fill_pos(0, translation_node ? translation_node->getCurve(0) : nullptr);
		fill_pos(1, translation_node ? translation_node->getCurve(1) : nullptr);
		fill_pos(2, translation_node ? translation_node->getCurve(2) : nullptr);

		for (Key& key : keys) {
			const ofbx::DMatrix mtx = bone.evalLocal({key.pos.x, key.pos.y, key.pos.z}, {key.rot.x, key.rot.y, key.rot.z});
			convert(mtx, key.pos, key.rot);
		}
	}

	const Bone* getParent(const Bone& bone) const {
		if (bone.parent_id == 0) return nullptr;
		for (const Bone& b : m_bones) {
			if (b.id == bone.parent_id) return &b;
		}
		ASSERT(false);
		return nullptr;
	}

	static float getScaleX(const ofbx::DMatrix& mtx) {
		Vec3 v(float(mtx.m[0]), float(mtx.m[4]), float(mtx.m[8]));
		return length(v);
	}

	void fillTracks(const ImportAnimation& anim
		, Array<Array<Key>>& tracks
		, u32 from_sample
		, u32 num_samples) const override
	{
		tracks.clear();
		tracks.reserve(m_bones.size());
		const ofbx::AnimationStack* fbx_anim = (const ofbx::AnimationStack*)m_scene->getAnimationStack(anim.index);
		const ofbx::AnimationLayer* layer = fbx_anim->getLayer(0);
		for (const Bone& bone : m_bones) {
			Array<Key>& keys = tracks.emplace(m_allocator);
			fill(*(const ofbx::Object*)(bone.id), *layer, keys, from_sample, num_samples, anim.fps);
		}

		for (const Bone& bone : m_bones) {
			const Bone* parent = getParent(bone);
			float scale = m_scene_scale;
			if (parent) {
				// parent_scale - animated scale is not supported, but we can get rid of static scale if we ignore
				// it in writeSkeleton() and use `parent_scale` in this function
				const ofbx::Object* fbx_parent = (const ofbx::Object*)(parent->id);
				const float parent_scale = (float)getScaleX(fbx_parent->getGlobalTransform());
				scale *= parent_scale;
			}
			if (fabsf(scale - 1) < 1e-5f) continue;
			
			Array<Key>& keys = tracks[u32(&bone - m_bones.begin())];
			for (Key& k : keys) k.pos *= scale;
		}

		if (m_orientation != Orientation::Y_UP) {
			for (Array<Key>& track : tracks) {
				for (Key& key : track) {
					key.pos = fixOrientation(key.pos);
					key.rot = fixOrientation(key.rot);
				}
			}
		}
	}

	static void getMaterialName(const ofbx::Material* material, char (&out)[128]) {
		copyString(out, material ? material->name : "default");
		char* iter = out;
		while (*iter) {
			char c = *iter;
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
				*iter = '_';
			}
			++iter;
		}
		makeLowercase(Span(out), out);
	}

	// TODO optimize this
	void getImportMeshName(ImportMesh& mesh, const ofbx::Mesh* fbx_mesh, HashMap<String, bool>& names, i32 submesh) const {
		const char* name = fbx_mesh->name;

		if (name[0] == '\0' && fbx_mesh->getParent()) name = fbx_mesh->getParent()->name;
		const ImportGeometry& geom = m_geometries[mesh.geometry_idx];
		if (name[0] == '\0') name = m_materials[geom.material_index].name.c_str();
		mesh.name = name;
		
		char* chars = mesh.name.getMutableData();
		for (u32 i = 0, len = mesh.name.length(); i < len; ++i) {
			// we use ':' as a separator between subresource:resource, so we can't have 
			// use it in mesh name
			if (chars[i] == ':') chars[i] = '_'; 
		}
		
		if (submesh >= 0) {
			char tmp[32];
			toCString(submesh, Span(tmp));
			mesh.name.append("_", tmp);
		}

		u32 collision = 0;
		StaticString<1024> tmp_name(mesh.name);
		for (;;) {
			bool found_collision = false;
			if (names.find(StringView(tmp_name)).isValid()) {
				++collision;
				found_collision = true;
				tmp_name = mesh.name;
				tmp_name.append(".", collision);
			}
			else {
				mesh.name = tmp_name;
				names.insert(mesh.name, true);
				break;
			}
		}
	}

	static i32 detectMeshLOD(StringView mesh_name) {
		const char* lod_str = findInsensitive(mesh_name, "_LOD");
		if (!lod_str) return 0;

		lod_str += stringLength("_LOD");

		i32 lod;
		fromCString(lod_str, lod);

		return lod;
	}

	void gatherLights(const ModelMeta& meta) {
		m_lights.reserve(m_scene->getLightCount());
		for (i32 i = 0, c = m_scene->getLightCount(); i < c; ++i) {
			const ofbx::Light* light = m_scene->getLight(i);
			const Matrix mtx = toLumix(light->getGlobalTransform());
			// TODO check if meta.scene_scale is applied everywhere
			Vec3 v = mtx.getTranslation() * meta.scene_scale * m_scene_scale;
			v = fixOrientation(v);
			const DVec3 pos = DVec3(v);
			m_lights.push(pos);
		}
	}

	void gatherMeshes(StringView fbx_filename, StringView src_dir, const ModelMeta* meta, bool ignore_geometry) {
		PROFILE_FUNCTION();
		const i32 c = m_scene->getMeshCount();

		Array<const ofbx::Material*> materials(m_allocator);
		HashMap<String, bool> names(m_allocator); 
		names.reserve(c);
		m_meshes.reserve(c);

		HashMap<GeomPartition, i32> geom_map(m_allocator);
		geom_map.reserve(c);

		for (int mesh_idx = 0; mesh_idx < c; ++mesh_idx) {
			const ofbx::Mesh* fbx_mesh = m_scene->getMesh(mesh_idx);
			const int mat_count = fbx_mesh->getMaterialCount();

			bool is_skinned = meta && meta->force_skin;
			const ofbx::Skin* skin = fbx_mesh->getSkin();
			if (skin) {
				for (int i = 0; i < skin->getClusterCount(); ++i) {
					if (skin->getCluster(i)->getIndicesCount() > 0) {
						is_skinned = true;
						break;
					}
				}
			}

			const ofbx::GeometryData& geom = fbx_mesh->getGeometryData();
			Span<const int> material_map(geom.getMaterialMap(), geom.getMaterialMapSize());
			
			// mesh can have materials, which are not used by any triangles
			// we don't want to create empty meshes for them
			// mark used materials/partitions
			StackArray<bool, 16> used_materials(m_allocator);
			used_materials.resize(mat_count);
			if (mat_count == 1) {
				used_materials[0] = true;
			}
			else {
				memset(used_materials.begin(), 0, used_materials.byte_size());
				for (i32 m : material_map) {
					if (m < 0) continue;
					if (m >= used_materials.size()) continue;
				
					used_materials[m] = true;
				}
			}
			
			for (int fbx_mat_index = 0; fbx_mat_index < mat_count; ++fbx_mat_index) {
				ofbx::GeometryPartition partition = geom.getPartition(mat_count > 1 ? fbx_mat_index : 0);
				
				if (material_map.length() != 0 && !used_materials[fbx_mat_index]) continue;

				const ofbx::Material* fbx_mat = fbx_mesh->getMaterial(fbx_mat_index);

				ImportMesh& mesh = m_meshes.emplace(m_allocator);
				mesh.mesh_index = m_meshes.size() - 1;
				m_fbx_meshes.push(fbx_mesh);

				i32 mat_idx = materials.indexOf(fbx_mat);
				if (mat_idx < 0) {
					mat_idx = materials.size();
					ImportMaterial& mat = m_materials.emplace(m_allocator);
					const ofbx::Color diffuse_color = fbx_mat->getDiffuseColor();
					mat.diffuse_color = { powf(diffuse_color.r, 2.2f), powf(diffuse_color.g, 2.2f), powf(diffuse_color.b, 2.2f) };
					materials.push(fbx_mat);
				}

				Matrix transform_matrix;
				Matrix geometry_matrix = toLumix(fbx_mesh->getGeometricMatrix());
				transform_matrix = toLumix(fbx_mesh->getGlobalTransform()) * geometry_matrix;
				transform_matrix.multiply3x3(m_scene_scale);
				transform_matrix.setTranslation(transform_matrix.getTranslation() * m_scene_scale);
				mesh.matrix = fixOrientation(transform_matrix);
				const bool flip_handness = doesFlipHandness(mesh.matrix);

				if (is_skinned) {
					ImportGeometry& import_geom = m_geometries.emplace(m_allocator);
					mesh.geometry_idx = m_geometries.size() - 1;
					import_geom.flip_handness = flip_handness;
					import_geom.is_skinned = is_skinned;
					import_geom.material_index = mat_idx;
					import_geom.submesh = mat_count > 1 ? fbx_mat_index : -1;
					FBXImportGeometry* fbx_geom = new (NewPlaceholder(), import_geom.user_data) FBXImportGeometry;
					fbx_geom->geom = &geom;
					fbx_geom->mesh = fbx_mesh;
				}
				else {
					const GeomPartition match = {
						.geom = &geom,
						.partition = u32(fbx_mat_index),
						.material = u32(mat_idx),
						.flip_handness = flip_handness
					};
					auto iter = geom_map.find(match);
					if (iter.isValid()) {
						mesh.geometry_idx = iter.value();
						ASSERT(!m_geometries[mesh.geometry_idx].is_skinned);
					}
					else {
						geom_map.insert(match, (i32)m_geometries.size());
						ImportGeometry& import_geom = m_geometries.emplace(m_allocator);
						import_geom.flip_handness = flip_handness;
						import_geom.is_skinned = false;
						mesh.geometry_idx = m_geometries.size() - 1;
						import_geom.material_index = mat_idx;
						import_geom.submesh = mat_count > 1 ? fbx_mat_index : -1;
						FBXImportGeometry* fbx_geom = new (NewPlaceholder(), import_geom.user_data) FBXImportGeometry;
						fbx_geom->geom = &geom;
						fbx_geom->mesh = fbx_mesh;
					}
				}

				getImportMeshName(mesh, fbx_mesh, names, mat_count > 1 ? fbx_mat_index : -1);
				m_geometries[mesh.geometry_idx].name = mesh.name; // TODO name from ofbx::Geometry
				mesh.lod = detectMeshLOD(mesh.name);

				if (doesFlipHandness(mesh.matrix)) {
					mesh.matrix.setXVector(mesh.matrix.getXVector() * -1);
				}
			}
		}

		// create material names
		for (u32 i = 0, num_mats = (u32)materials.size(); i < num_mats; ++i) {
			ImportMaterial& mat = m_materials[i];
			char name[128];
			getMaterialName(materials[i], name);

			char orig_name[128];
			copyString(orig_name, name);

			// check name collisions
			u32 collision = 0;
			for (;;) {
				bool collision_found = false;
				
				for (u32 j = 0; j < i; ++j) {
					if (m_materials[j].name == name) {
						copyString(name, orig_name);
						char num[16];
						++collision;
						// there's collision, add number at the end of the name
						toCString(collision, Span(num));
						catString(name, num);
						collision_found = true;
						break;
					}
				}

				if (!collision_found) break;
			}

			mat.name = name;
		}

		// TODO move this to modelimporter?
		// gather textures
		// we don't support dds, but try it as last option, so user can get error message with filepath
		const char* exts[] = { "png", "jpg", "jpeg", "tga", "bmp", "dds" };
		FileSystem& filesystem = m_app.getEngine().getFileSystem();
		for (u32 i = 0, num_mats = (u32)m_materials.size(); i < num_mats; ++i) {
			ImportMaterial& mat = m_materials[i];
			auto gatherTexture = [&](ofbx::Texture::TextureType type) {
				const ofbx::Texture* texture = materials[i]->getTexture(type);
				if (!texture) return;

				ImportTexture& tex = mat.textures[type];
				ofbx::DataView filename = texture->getRelativeFileName();
				if (filename == "") filename = texture->getFileName();
				tex.path = toStringView(filename);
				tex.src = tex.path;
				tex.import = filesystem.fileExists(tex.src);

				StringView tex_ext = Path::getExtension(tex.path);
				if (!tex.import && (equalStrings(tex_ext, "dds") || !findTexture(src_dir, tex_ext, tex))) {
					for (const char*& ext : exts) {
						if (findTexture(src_dir, ext, tex)) {
							// we assume all texture have the same extension,
							// so we move it to the beginning, so it's checked first
							swap(ext, exts[0]);
							break;
						}
					}
				}

				Path::normalize(tex.src.data);

				if (!tex.import) {
					logInfo(fbx_filename, ": texture ", tex.src, " not found");
					tex.src = "";
				}

				tex.import = true;
			};

			gatherTexture(ofbx::Texture::DIFFUSE);
			gatherTexture(ofbx::Texture::NORMAL);
			gatherTexture(ofbx::Texture::SPECULAR);
		}
	}

	static const inline int B64index[256] = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62, 63, 62, 62, 63, 52, 53, 54, 55,
		56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
		7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,
		0,  0,  0, 63,  0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
		41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
	};

	static void decodeBase64(const void* data, const u32 len, OutputMemoryStream& str)
	{
		unsigned char* p = (unsigned char*)data;
		int pad = len > 0 && (len % 4 || p[len - 1] == '=');
		const u32 L = ((len + 3) / 4 - pad) * 4;
		const u32 offset = (u32)str.size();
		str.resize(L / 4 * 3 + pad + offset);

		for (u32 i = 0, j = 0; i < L; i += 4)
		{
			int n = B64index[p[i]] << 18 | B64index[p[i + 1]] << 12 | B64index[p[i + 2]] << 6 | B64index[p[i + 3]];
			str[offset + j++] = n >> 16;
			str[offset + j++] = n >> 8 & 0xFF;
			str[offset + j++] = n & 0xFF;
		}
		if (pad)
		{
			int n = B64index[p[L]] << 18 | B64index[p[L + 1]] << 12;
			str[u32(str.size() - 1)] = n >> 16;

			if (len > L + 2 && p[L + 2] != '=')
			{
				n |= B64index[p[L + 2]] << 6;
				str.write(u8(n >> 8 & 0xFF));
			}
		}
	}

	static void extractEmbedded(const ofbx::IScene& m_scene, StringView src_dir, IAllocator& allocator)
	{
		PROFILE_FUNCTION();
		for (int i = 0, c = m_scene.getEmbeddedDataCount(); i < c; ++i) {
			const ofbx::DataView embedded = m_scene.getEmbeddedData(i);

			StringView filename = toStringView(m_scene.getEmbeddedFilename(i));
			const PathInfo pi(filename);
			const StaticString<MAX_PATH> fullpath(src_dir, pi.basename, ".", pi.extension);

			if (os::fileExists(fullpath)) continue;

			os::OutputFile file;
			if (!file.open(fullpath)) {
				logError("Failed to save ", fullpath);
				return;
			}

			if (m_scene.isEmbeddedBase64(i)) {
				OutputMemoryStream tmp(allocator);
				const ofbx::IElementProperty* prop = m_scene.getEmbeddedBase64Data(i);
				if (prop) {
					if (prop->getNext()) {
						for (const auto* j = prop; j; j = j->getNext()) {
							decodeBase64(j->getValue().begin, u32(j->getValue().end - j->getValue().begin), tmp);
						}
					}
					else {
						decodeBase64(prop->getValue().begin, u32(prop->getValue().end - prop->getValue().begin), tmp);
					}
					if (!file.write(tmp.data(), tmp.size())) {
						logError("Failed to write ", fullpath);
					}
				}
				else logError("Invalid data ", fullpath);
			}
			else {
				if (!file.write(embedded.begin + 4, embedded.end - embedded.begin - 4)) {
					logError("Failed to write ", fullpath);
				}
			}
			file.close();
		}
	}

	static void ofbx_job_processor(ofbx::JobFunction fn, void*, void* data, u32 size, u32 count) {
		jobs::forEach(count, 1, [data, size, fn](i32 i, i32){
			PROFILE_BLOCK("ofbx job");
			u8* ptr = (u8*)data;
			fn(ptr + i * size);
		});
	}

	bool parse(const Path& filename, const ModelMeta& meta) override {
		return parseInternal(filename, &meta);
	}

	bool parseSimple(const Path& filename) override {
		return parseInternal(filename, nullptr);
	}
	
	bool parseInternal(const Path& filename, const ModelMeta* meta) {
		PROFILE_FUNCTION();
		const bool ignore_geometry = meta == nullptr;

		ASSERT(!m_scene);
		OutputMemoryStream data(m_allocator);
		{
			PROFILE_BLOCK("load file");
			FileSystem& fs = m_app.getEngine().getFileSystem();
			if (!fs.getContentSync(Path(filename), data)) return false;
		}
		
		const ofbx::LoadFlags flags = ignore_geometry ? ofbx::LoadFlags::IGNORE_GEOMETRY | ofbx::LoadFlags::KEEP_MATERIAL_MAP : ofbx::LoadFlags::NONE;
		{
			PROFILE_BLOCK("ofbx::load");
			m_scene = ofbx::load(data.data(), (i32)data.size(), static_cast<u16>(flags), &ofbx_job_processor, nullptr);
		}
		if (!m_scene)
		{
			logError("Failed to import \"", filename, ": ", ofbx::getError(), "\n"
				"Please try to convert the FBX file with Autodesk FBX Converter or some other software to the latest version.");
			return false;
		}
		m_scene_scale = m_scene->getGlobalSettings()->UnitScaleFactor * 0.01f;
		if (meta) m_scene_scale *= meta->scene_scale;

		const ofbx::GlobalSettings* settings = m_scene->getGlobalSettings();
		switch (settings->UpAxis) {
			case ofbx::UpVector_AxisX: m_orientation = Orientation::X_UP; break;
			case ofbx::UpVector_AxisY: m_orientation = Orientation::Y_UP; break;
			case ofbx::UpVector_AxisZ: m_orientation = Orientation::Z_UP; break;
		}

		StringView src_dir = Path::getDir(filename);
		if (!ignore_geometry) extractEmbedded(*m_scene, src_dir, m_allocator);

		gatherMeshes(filename, src_dir, meta, ignore_geometry);
		if(!meta || !meta->ignore_animations) gatherAnimations(filename);
		if (meta) gatherLights(*meta);

		if (!ignore_geometry) {
			bool any_skinned = false;
			for (const ImportGeometry& g : m_geometries) any_skinned = any_skinned || g.is_skinned;
			// TODO why do we need this here?
			gatherBones(meta->force_skin || any_skinned);
		}

		if (m_bones.empty() && m_meshes.empty() && m_animations.empty()) {
			logError(filename, ": found nothing to import");
			return false;
		}

		if (meta) postprocess(*meta, filename);

		return true;
	}

	IAllocator& m_allocator;
	Array<const ofbx::Mesh*> m_fbx_meshes;
	ofbx::IScene* m_scene;
	Orientation m_orientation = Orientation::Y_UP;
	float m_scene_scale = 1.f;
};

ModelImporter* createFBXImporter(StudioApp& app, IAllocator& allocator) {
	return LUMIX_NEW(allocator, FBXImporter)(app, allocator);
}

void destroyFBXImporter(ModelImporter& importer) {
	FBXImporter& fbx_importer = static_cast<FBXImporter&>(importer);
	IAllocator& allocator = fbx_importer.m_allocator;
	LUMIX_DELETE(allocator, &fbx_importer);
}


} // namespace Lumix