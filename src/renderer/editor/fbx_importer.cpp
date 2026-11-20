#include "fbx_importer.h"
#include "animation/animation.h"
#include "core/atomic.h"
#include "core/crt.h"
#include "core/sort.h"
#include "core/hash.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/stack_array.h"
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/engine_hash_funcs.h"
#include "engine/prefab.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "meshoptimizer/meshoptimizer.h"
#include "mikktspace/mikktspace.h"
#include "ufbx/ufbx.h"
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

// ufbx thread pool backed by our job system
// ufbx calls run/wait from the thread which called ufbx_load_memory, that must be a job
struct UfbxJobPool {
	jobs::Counter counters[UFBX_THREAD_GROUP_COUNT];

	static void run(void* user, ufbx_thread_pool_context ctx, uint32_t group, uint32_t start_index, uint32_t count) {
		UfbxJobPool* pool = (UfbxJobPool*)user;
		for (uint32_t i = start_index; i < start_index + count; ++i) {
			jobs::runLambda([ctx, i]() {
				PROFILE_BLOCK("ufbx task");
				ufbx_thread_pool_run_task(ctx, i);
			}, &pool->counters[group]);
		}
	}

	static void wait(void* user, ufbx_thread_pool_context ctx, uint32_t group, uint32_t max_index) {
		UfbxJobPool* pool = (UfbxJobPool*)user;
		jobs::wait(&pool->counters[group]);
	}
};

struct FBXImportGeometry {
	const ufbx_mesh* geom;
	const ufbx_node* mesh; // node instancing `geom`
	u32 part; // index into geom->material_parts
	i32 bone_idx = -1;
};

struct GeomPartition {
	const ufbx_mesh* geom;
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

	FBXImporter(StudioApp& app, IAllocator& allocator)
		: ModelImporter(app)
		, m_allocator(allocator)
		, m_scene(nullptr)
		, m_fbx_meshes(m_allocator)
	{}

	~FBXImporter()
	{
		if (m_scene) ufbx_free_scene(m_scene);
		if (m_impostor_shadow_shader) m_impostor_shadow_shader->decRefCount();
	}

	static StringView toStringView(ufbx_string str) {
		return StringView(str.data, str.data + str.length);
	}

	static bool isProp(const ufbx_anim_prop& prop, const char* name) {
		return stringLength(name) == prop.prop_name.length && memcmp(prop.prop_name.data, name, prop.prop_name.length) == 0;
	}

	// parent of `node`, null for top-level nodes (ufbx has an explicit root node)
	static const ufbx_node* getParentNode(const ufbx_node* node) {
		return node->parent && !node->parent->is_root ? node->parent : nullptr;
	}

	static const ufbx_skin_deformer* getSkin(const ufbx_mesh* mesh) {
		return mesh && mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
	}

	static bool isConstCurve(const ufbx_anim_curve* curve) {
		if (!curve) return true;
		if (curve->keyframes.count <= 1) return true;
		if (curve->keyframes.count == 2 && fabs(curve->keyframes.data[1].value - curve->keyframes.data[0].value) < 1e-6) return true;
		return false;
	}

	static Vec3 toLumixVec3(const ufbx_vec3& v) { return {(float)v.x, (float)v.y, (float)v.z}; }

	static Matrix toLumix(const ufbx_matrix& mtx) {
		Matrix res = Matrix::IDENTITY;
		for (int i = 0; i < 4; ++i) {
			res.columns[i].x = (float)mtx.cols[i].x;
			res.columns[i].y = (float)mtx.cols[i].y;
			res.columns[i].z = (float)mtx.cols[i].z;
			res.columns[i].w = i == 3 ? 1.f : 0.f;
		}
		return res;
	}

	static u32 packColor(const ufbx_vec4& vec) {
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

	static bool hasTangents(const ufbx_mesh& geom) {
		if (geom.vertex_tangent.exists) return true;
		if (geom.vertex_uv.exists) return true;
		return false;
	}

	static int getVertexSize(const ufbx_mesh& geom, bool is_skinned, const ModelMeta& meta) {
		static const int POSITION_SIZE = sizeof(float) * 3;
		static const int NORMAL_SIZE = sizeof(u8) * 4;
		static const int TANGENT_SIZE = sizeof(u8) * 4;
		static const int UV_SIZE = sizeof(float) * 2;
		static const int COLOR_SIZE = sizeof(u8) * 4;
		static const int AO_SIZE = sizeof(u8) * 4;
		static const int BONE_INDICES_WEIGHTS_SIZE = sizeof(u16) * 4 + sizeof(u16) * 4;
		int size = POSITION_SIZE + NORMAL_SIZE;

		if (geom.vertex_uv.exists) size += UV_SIZE;
		if (meta.bake_vertex_ao) size += AO_SIZE;
		if (geom.vertex_color.exists && meta.import_vertex_colors) size += meta.vertex_color_is_ao ? AO_SIZE : COLOR_SIZE;
		if (hasTangents(geom)) size += TANGENT_SIZE;
		if (is_skinned) size += BONE_INDICES_WEIGHTS_SIZE;

		return size;
	}

	static bool areIndices16Bit(const ImportGeometry& mesh) {
		int vertex_size = mesh.vertex_size;
		return mesh.vertex_buffer.size() / vertex_size < (1 << 16);
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

	static void resetTangents(OutputMemoryStream& unindexed_triangles, const VertexLayout& layout) {
		const u32 vertex_size = layout.size;
		const u32 num_vertices = u32(unindexed_triangles.size() / vertex_size);
		u8* tangents = unindexed_triangles.getMutableData() + layout.tangent_offset;
		for (u32 i = 0; i < num_vertices; ++i) {
			memset(tangents + i * vertex_size, 0, 4);
		}
	}

	static void computeTangentsSimple(ImportGeometry& geom, const VertexLayout& layout, IAllocator& allocator) {
		PROFILE_FUNCTION();
		const u32 vertex_size = layout.size;
		const u32 num_vertices = u32(geom.vertex_buffer.size() / vertex_size);
		const u32* indices = geom.indices.data();
		const u8* positions = geom.vertex_buffer.data();
		const u8* uvs = positions + layout.uv_offset;
		const u32 num_indices = geom.indices.size();

		Array<Vec3> tangents(allocator);
		tangents.resize(num_vertices);
		memset(tangents.begin(), 0, tangents.byte_size());

		for (u32 i = 0; i < num_indices; i += 3) {
			Vec3 v0; memcpy(&v0, positions + indices[i] * vertex_size, sizeof(v0));
			Vec3 v1; memcpy(&v1, positions + indices[i + 1] * vertex_size, sizeof(v1));
			Vec3 v2; memcpy(&v2, positions + indices[i + 2] * vertex_size, sizeof(v2));
			Vec2 uv0; memcpy(&uv0, uvs + indices[i] * vertex_size, sizeof(uv0));
			Vec2 uv1; memcpy(&uv1, uvs + indices[i + 1] * vertex_size, sizeof(uv1));
			Vec2 uv2; memcpy(&uv2, uvs + indices[i + 2] * vertex_size, sizeof(uv2));

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
			
			tangents[indices[i]] += tangent;
			tangents[indices[i + 1]] += tangent;
			tangents[indices[i + 2]] += tangent;
		}

		u8* packed_tangents = geom.vertex_buffer.getMutableData() + layout.tangent_offset;
		for (u32 i = 0; i < num_vertices; ++i) {
			u32 tangent_packed = packF4u(normalize(tangents[i]));
			memcpy(packed_tangents + i * vertex_size, &tangent_packed, sizeof(tangent_packed));
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

	// convert from ufbx to runtime vertex data, compute missing info (normals, tangents, ao, ...)
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

				const ufbx_mesh& geom = *fbx_geom->geom;
				import_geom.vertex_size = getVertexSize(geom, import_geom.is_skinned, meta);
				const ufbx_mesh_part& partition = geom.material_parts.data[fbx_geom->part];
				if (partition.num_triangles == 0) continue;

				import_geom.attributes.push({
					.semantic = AttributeSemantic::POSITION,
					.type = AttributeType::FLOAT,
					.num_components = 3
				});

				import_geom.attributes.push({
					.semantic = AttributeSemantic::NORMAL,
					.type = AttributeType::I8,
					.num_components = 4
				});

				if (geom.vertex_uv.exists) {
					import_geom.attributes.push({
						.semantic = AttributeSemantic::TEXCOORD0,
						.type = AttributeType::FLOAT,
						.num_components = 2
					});
				}

				if (meta.bake_vertex_ao) {
					import_geom.attributes.push({
						.semantic = AttributeSemantic::AO,
						.type = AttributeType::U8,
						.num_components = 4 // 1+3 because of padding
					});
				}
				if (geom.vertex_color.exists && meta.import_vertex_colors) {
					if (meta.vertex_color_is_ao) {
						import_geom.attributes.push({
							.semantic = AttributeSemantic::AO,
							.type = AttributeType::U8,
							.num_components = 4 // 1+3 because of padding
						});
					}
					else {
						import_geom.attributes.push({
							.semantic = AttributeSemantic::COLOR0,
							.type = AttributeType::U8,
							.num_components = 4
						});
					}
				}

				if (hasTangents(geom)) {
					import_geom.attributes.push({
						.semantic = AttributeSemantic::TANGENT,
						.type = AttributeType::I8,
						.num_components = 4
					});
				}

				if (import_geom.is_skinned) {
					import_geom.attributes.push({
						.semantic = AttributeSemantic::JOINTS,
						.type = AttributeType::U16,
						.num_components = 4
					});
					import_geom.attributes.push({
						.semantic = AttributeSemantic::WEIGHTS,
						.type = AttributeType::U16,
						.num_components = 4
					});
				}

				PROFILE_BLOCK("FBX convert vertex data")
				profiler::pushInt("Triangle count", (i32)partition.num_triangles);

				const bool has_normals = geom.vertex_normal.exists;
				const bool has_tangents = geom.vertex_tangent.exists;
				const bool has_colors = meta.import_vertex_colors && geom.vertex_color.exists;
				const bool has_uvs = geom.vertex_uv.exists;

				VertexLayout vertex_layout;

				const bool compute_tangents = !has_tangents && has_uvs || meta.force_recompute_tangents;

				vertex_layout.size = sizeof(Vec3); // position
				vertex_layout.normal_offset = vertex_layout.size;
				vertex_layout.size += sizeof(u32); // normals
				vertex_layout.uv_offset = vertex_layout.size;
				if (has_uvs) vertex_layout.size += sizeof(Vec2);
				if (meta.bake_vertex_ao) vertex_layout.size += sizeof(u32);
				if (has_colors) vertex_layout.size += sizeof(u32);
				vertex_layout.tangent_offset = vertex_layout.size;
				if (has_tangents || compute_tangents) vertex_layout.size += sizeof(u32);
				if (import_geom.is_skinned) vertex_layout.size += 4 * sizeof(u16) + 4 * sizeof(u16);

				if (import_geom.is_skinned) {
					const FBXImportGeometry* g = (const FBXImportGeometry*)import_geom.user_data;
					fillSkinInfo(skinning, g->bone_idx, fbx_geom->mesh);
					triangulate(unindexed_triangles, import_geom, partition, geom, &skinning, meta, tri_indices_tmp);
				}
				else {
					triangulate(unindexed_triangles, import_geom, partition, geom, nullptr, meta, tri_indices_tmp);
				}

				if (!has_normals || meta.force_recompute_normals) computeNormals(unindexed_triangles, vertex_layout.size, vertex_layout.normal_offset);

				if (compute_tangents) {
					if (meta.use_mikktspace) {
						computeTangents(unindexed_triangles, vertex_layout, path);
					}
					else {
						resetTangents(unindexed_triangles, vertex_layout);
					}
				}
				
				remap(unindexed_triangles, import_geom);
				if (compute_tangents && !meta.use_mikktspace) {
					computeTangentsSimple(import_geom, vertex_layout, m_allocator);
				}
				import_geom.index_size = areIndices16Bit(import_geom) ? 2 : 4;
				
				if (import_geom.flip_handness) {
					for (i32 i = 0, n = import_geom.indices.size(); i < n; i += 3) {
						swap(import_geom.indices[i], import_geom.indices[i + 1]);
					}
				}
			}
		});

		postprocessCommon(meta, path);
}

	void insertHierarchy(const ufbx_node* node) {
		if (!node) return;
		if (m_bones.find([&](const Bone& bone){ return bone.id == u64(node); }) >= 0) return;

		insertHierarchy(getParentNode(node));
		Bone& bone = m_bones.emplace(m_allocator);
		bone.id = u64(node);
	}


	static ufbx_matrix getBindPoseMatrix(const ufbx_node* mesh_node, const ufbx_node* node) {
		if (!mesh_node) return node->node_to_world;

		const ufbx_skin_deformer* skin = getSkin(mesh_node->mesh);
		if (!skin) return node->node_to_world;

		for (const ufbx_skin_cluster* cluster : skin->clusters) {
			if (cluster->bone_node == node) {
				return cluster->bind_to_world;
			}
		}
		return node->node_to_world;
	}

	void gatherBones(bool force_skinned) {
		PROFILE_FUNCTION();
		for (const ImportMesh& mesh : m_meshes) {
			const ufbx_node* fbx_mesh = m_fbx_meshes[mesh.mesh_index];
			const ufbx_skin_deformer* skin = getSkin(fbx_mesh->mesh);
			if (skin) {
				for (const ufbx_skin_cluster* cluster : skin->clusters) {
					insertHierarchy(cluster->bone_node);
				}
			}

			if (force_skinned) {
				insertHierarchy(fbx_mesh);
			}
		}

		for (const ufbx_anim_stack* stack : m_scene->anim_stacks) {
			for (const ufbx_anim_layer* layer : stack->layers) {
				for (const ufbx_anim_prop& prop : layer->anim_props) {
					if (prop.element->type != UFBX_ELEMENT_NODE) continue;
					if (!isProp(prop, "Lcl Translation") && !isProp(prop, "Lcl Rotation") && !isProp(prop, "Lcl Scaling")) continue;
					insertHierarchy((const ufbx_node*)prop.element);
				}
			}
		}

		m_bones.removeDuplicates();
		for (Bone& bone : m_bones) {
			const ufbx_node* node = (const ufbx_node*)bone.id;
			const ufbx_node* parent = getParentNode(node);
			bone.parent_id = parent ? u64(parent) : 0;
		}

		sortBones();

		if (force_skinned) {
			for (ImportGeometry& g : m_geometries) {
				FBXImportGeometry* fbx_geom = (FBXImportGeometry*)g.user_data;
				fbx_geom->bone_idx = m_bones.find([&](const Bone& bone){ return bone.id == u64(fbx_geom->mesh); });
			}
		}

		for (Bone& bone : m_bones) {
			const ufbx_node* node = (const ufbx_node*)bone.id;
			const ufbx_node* mesh = getAnyMeshFromBone(node, i32(&bone - m_bones.begin()));
			Matrix tr = toLumix(getBindPoseMatrix(mesh, node));
			tr.normalizeScale(); // TODO why?
			bone.bind_pose_matrix = tr;
			bone.name = toStringView(node->name);
		}
	}

	LUMIX_FORCE_INLINE u32 getPackedVec3(ufbx_vec3 vec) const {
		Vec3 v = toLumixVec3(vec);
		return packF4u(v);
	}

	void fillSkinInfo(Array<Skin>& skinning, i32 force_bone_idx, const ufbx_node* node) const {
		const ufbx_mesh* mesh = node->mesh;
		const ufbx_skin_deformer* fbx_skin = getSkin(mesh);
		skinning.resize((u32)mesh->num_vertices);
		if (!skinning.empty()) memset(&skinning[0], 0, skinning.size() * sizeof(skinning[0]));

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

		Array<i32> cluster_joints(m_allocator);
		cluster_joints.resize((u32)fbx_skin->clusters.count);
		for (i32 i = 0; i < cluster_joints.size(); ++i) {
			const ufbx_node* link = fbx_skin->clusters.data[i]->bone_node;
			cluster_joints[i] = link ? m_bones.find([&](const Bone& bone){ return bone.id == u64(link); }) : -1;
		}

		const u32 num_vertices = (u32)minimum(mesh->num_vertices, fbx_skin->vertices.count);
		for (u32 vi = 0; vi < num_vertices; ++vi) {
			const ufbx_skin_vertex sv = fbx_skin->vertices.data[vi];
			Skin& s = skinning[vi];
			for (u32 j = 0; j < sv.num_weights; ++j) {
				const ufbx_skin_weight w = fbx_skin->weights.data[sv.weight_begin + j];
				const i32 joint = cluster_joints[w.cluster_index];
				if (joint < 0) continue;
				const float weight = (float)w.weight;
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
		, const ufbx_mesh_part& partition
		, const ufbx_mesh& geom
		, const Array<Skin>* skinning
		, const ModelMeta& meta
		, Array<i32>& tri_indices) const
	{
		PROFILE_FUNCTION();
		const bool has_normals = geom.vertex_normal.exists;
		const bool has_tangents = geom.vertex_tangent.exists;
		const bool has_colors = meta.import_vertex_colors && geom.vertex_color.exists;
		const bool has_uvs = geom.vertex_uv.exists;
		const bool compute_tangents = !has_tangents && has_uvs;

		tri_indices.resize(maximum((u32)geom.max_face_triangles, 1u) * 3);
		unindexed_triangles.clear();
		unindexed_triangles.resize(mesh.vertex_size * 3 * partition.num_triangles);
		u8* dst = unindexed_triangles.getMutableData();
		// convert to interleaved vertex data of unindexed triangles
		//	tri[0].v[0].pos, tri[0].v[0].normal, ... tri[0].v[2].tangent, tri[1].v[0].pos, ...
		auto write = [&](const auto& v){
			memcpy(dst, &v, sizeof(v));
			dst += sizeof(v);
		};

		for (uint32_t face_idx : partition.face_indices) {
			const ufbx_face face = geom.faces.data[face_idx];
			const u32 tri_count = ufbx_triangulate_face((uint32_t*)tri_indices.begin(), tri_indices.size(), &geom, face) * 3;
			for (u32 i = 0; i < tri_count; ++i) {
				const u32 corner = tri_indices[i];
				write(toLumixVec3(ufbx_get_vertex_vec3(&geom.vertex_position, corner)));

				if (has_normals) write(getPackedVec3(ufbx_get_vertex_vec3(&geom.vertex_normal, corner)));
				else write(u32(0));
				if (has_uvs) {
					const ufbx_vec2 uv = ufbx_get_vertex_vec2(&geom.vertex_uv, corner);
					write(Vec2((float)uv.x, 1 - (float)uv.y));
				}
				if (meta.bake_vertex_ao) write(u32(0));
				if (has_colors) {
					const ufbx_vec4 color = ufbx_get_vertex_vec4(&geom.vertex_color, corner);
					if (meta.vertex_color_is_ao) {
						const u8 ao[4] = { u8(color.x * 255.f + 0.5f) };
						memcpy(dst, ao, sizeof(ao));
						dst += sizeof(ao);
					} else {
						write(packColor(color));
					}
				}
				if (has_tangents) write(getPackedVec3(ufbx_get_vertex_vec3(&geom.vertex_tangent, corner)));
				else if (compute_tangents) write(u32(0));
				if (skinning) {
					const Skin* skin = &(*skinning)[geom.vertex_indices.data[corner]];
					write(skin->joints);
					for (float fw : skin->weights) {
						u16 w = u16(clamp(fw * 0xffFF, 0.f, 65535.f));
						write(w);
					}
				}
			}
		}
		ASSERT(dst - unindexed_triangles.data() == unindexed_triangles.size());
	}

	struct BoneNode {
		Bone* bone;
		u32 bone_index;
		BoneNode* parent;
		BoneNode* first_child;
		BoneNode* next_sibling;
		u32 num_children;
	};

	static i32 findBestCandidate(Span<BoneNode*> queue, Span<BoneNode*> sorted) {
		i32 batch_start = sorted.size() & ~3;
		for (u32 i = 0; i < queue.size(); ++i) {
			bool pass = true;
			for (u32 j = batch_start; j < sorted.size(); ++j) {
				if (queue[i]->bone->parent_id == sorted[j]->bone->id) {
					pass = false;
					break;
				}
			}
			if (pass) return i;
		}

		return 0;
	}

	// sort bones for optimal simd processing (4 bones in a row must have parents in the previous bones) 
	void sortBones() {
		if (m_bones.size() <= 1) return;
		
		HashMap<u64, i32> id_to_index(m_allocator);
		for (i32 i = 0; i < m_bones.size(); ++i) {
			const Bone& b = m_bones[i];
			id_to_index.insert(b.id, i);
		}

		Array<BoneNode> nodes(m_allocator);
		nodes.resize(m_bones.size());
		for (i32 i = 0; i < m_bones.size(); ++i) {
			nodes[i].bone = &m_bones[i];
			nodes[i].bone_index = i;
			nodes[i].parent = nullptr;
			nodes[i].first_child = nullptr;
			nodes[i].next_sibling = nullptr;
			nodes[i].num_children = 0;
		}

		for (i32 i = 0; i < m_bones.size(); ++i) {
			if (m_bones[i].parent_id == 0) continue;

			i32 parent_index = id_to_index[m_bones[i].parent_id];
			nodes[i].parent = &nodes[parent_index];
			nodes[i].next_sibling = nodes[parent_index].first_child;
			nodes[parent_index].first_child = &nodes[i];
			++nodes[parent_index].num_children;
		}

		Array<BoneNode*> queue(m_allocator);
		for (BoneNode& node : nodes) {
			if (!node.parent) queue.push(&node);
		}
		Array<BoneNode*> sorted(m_allocator);
		while (!queue.empty()) {
			i32 idx = findBestCandidate(queue, sorted);
			BoneNode& node = *queue[idx];
			queue.erase(idx);
			sorted.push(&node);
			
			u32 bone_idx = u32(&node - nodes.begin());
			Array<Bone*> children(m_allocator);
			for (BoneNode* child = node.first_child; child; child = child->next_sibling) {
				u32 child_idx = u32(child - nodes.begin());
				u32 num = child->num_children;
				i32 insert_pos = 0;
				for (; insert_pos < queue.size(); ++insert_pos) {
					u32 q_idx = u32(queue[insert_pos] - nodes.begin());
					if (num > nodes[q_idx].num_children) break;
				}
				queue.insert(insert_pos, child);
			}
		}

		Array<Bone> new_bones(m_allocator);
		new_bones.reserve(m_bones.size());
		for (BoneNode* b : sorted) {
			new_bones.push(*b->bone);
		}
		m_bones = static_cast<Array<Bone>&&>(new_bones);

	}

	const ufbx_node* getAnyMeshFromBone(const ufbx_node* node, i32 bone_idx) const {
		for (const ImportGeometry& geom : m_geometries) {
			FBXImportGeometry* fbx_geom = (FBXImportGeometry*)geom.user_data;
			if (fbx_geom->bone_idx == bone_idx) {
				return fbx_geom->mesh;
			}

			const ufbx_skin_deformer* skin = getSkin(fbx_geom->geom);
			if (!skin) continue;

			for (const ufbx_skin_cluster* cluster : skin->clusters) {
				if (cluster->bone_node == node) return fbx_geom->mesh;
			}
		}
		return nullptr;
	}

	void gatherAnimations(StringView src) {
		PROFILE_FUNCTION();
		for (u32 i = 0; i < m_scene->anim_stacks.count; ++i) {
			ImportAnimation& anim = m_animations.emplace();
			anim.index = i;
			const ufbx_anim_stack* fbx_anim = m_scene->anim_stacks.data[i];
			anim.name = toStringView(fbx_anim->name);
			if (anim.name.empty()) anim.name = "anim";
			anim.fps = (float)m_scene->settings.frames_per_second;
			anim.length = fbx_anim->time_end - fbx_anim->time_begin;

			const ufbx_anim_layer* anim_layer = fbx_anim->layers.count > 0 ? fbx_anim->layers.data[0] : nullptr;
			if (!anim_layer || anim_layer->anim_props.count == 0) {
				m_animations.pop();
				continue;
			}

			bool data_found = false;
			for (const ufbx_anim_prop& prop : anim_layer->anim_props) {
				if (prop.element->type != UFBX_ELEMENT_NODE) continue;
				if (!isProp(prop, "Lcl Translation") && !isProp(prop, "Lcl Rotation")) continue;
				if (!isConstCurve(prop.anim_value->curves[0]) || !isConstCurve(prop.anim_value->curves[1]) || !isConstCurve(prop.anim_value->curves[2])) {
					data_found = true;
					break;
				}
			}
			if (!data_found) m_animations.pop();
		}

		if (m_animations.size() == 1) {
			m_animations[0].name = "";
		}
	}

	static void fill(const ufbx_node& bone, const ufbx_anim_layer& layer, Array<Key>& keys, u32 from_sample, u32 samples_count, float fps) {
		bool animated = false;
		for (const ufbx_anim_prop& prop : layer.anim_props) {
			if (prop.element != &bone.element) continue;
			if (isProp(prop, "Lcl Translation") || isProp(prop, "Lcl Rotation")) {
				animated = true;
				break;
			}
		}
		if (!animated) return;

		keys.resize(samples_count);
		for (u32 f = 0; f < samples_count; ++f) {
			const ufbx_transform tr = ufbx_evaluate_transform(layer.anim, &bone, (from_sample + f) / double(fps));
			Key& k = keys[f];
			k.pos = toLumixVec3(tr.translation);
			k.rot = normalize(Quat((float)tr.rotation.x, (float)tr.rotation.y, (float)tr.rotation.z, (float)tr.rotation.w));
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

	static float getScaleX(const ufbx_matrix& mtx) {
		Vec3 v((float)mtx.cols[0].x, (float)mtx.cols[1].x, (float)mtx.cols[2].x);
		return length(v);
	}

	void fillTracks(const ImportAnimation& anim
		, Array<Array<Key>>& tracks
		, u32 from_sample
		, u32 num_samples) const override
	{
		tracks.clear();
		tracks.reserve(m_bones.size());
		const ufbx_anim_stack* fbx_anim = m_scene->anim_stacks.data[anim.index];
		const ufbx_anim_layer* layer = fbx_anim->layers.data[0];
		for (const Bone& bone : m_bones) {
			Array<Key>& keys = tracks.emplace(m_allocator);
			fill(*(const ufbx_node*)(bone.id), *layer, keys, from_sample, num_samples, anim.fps);
		}

		for (const Bone& bone : m_bones) {
			const Bone* parent = getParent(bone);
			if (!parent) continue;
			// animated scale is not supported, but we can get rid of static scale (including the unit conversion
			// done by ufbx) if we ignore it in writeSkeleton() and use `parent_scale` here
			const ufbx_node* fbx_parent = (const ufbx_node*)(parent->id);
			const float scale = getScaleX(fbx_parent->node_to_world);
			if (fabsf(scale - 1) < 1e-5f) continue;

			Array<Key>& keys = tracks[u32(&bone - m_bones.begin())];
			for (Key& k : keys) k.pos *= scale;
		}
	}

	static void getMaterialName(const ufbx_material* material, char (&out)[128]) {
		copyString(out, material ? material->name.data : "default");
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
	void getImportMeshName(ImportMesh& mesh, const ufbx_node* fbx_mesh, HashMap<String, bool>& names, i32 submesh) const {
		const char* name = fbx_mesh->name.data;

		const ufbx_node* parent = getParentNode(fbx_mesh);
		if (name[0] == '\0' && parent) name = parent->name.data;
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
		m_lights.reserve((u32)m_scene->lights.count);
		for (const ufbx_node* node : m_scene->nodes) {
			if (!node->light) continue;
			const Matrix mtx = toLumix(node->node_to_world);
			const DVec3 pos = DVec3(mtx.getTranslation());
			m_lights.push(pos);
		}
	}

	void gatherObjects() {
		m_objects.clear();
		for (const ufbx_node* object : m_scene->nodes) {
			// Meshes are represented by model entities. Other FBX nodes become
			// empty entities when the corresponding option is enabled.
			if (object->is_root || object->mesh) continue;

			const ufbx_node* parent = getParentNode(object);
			ImportObject& out = m_objects.emplace(m_allocator);
			out.id = u64(object);
			out.parent_id = parent ? u64(parent) : 0;
			out.matrix = toLumix(object->node_to_world);
			out.name = object->name.length ? object->name.data : "object";
		}
	}

	void gatherMeshes(StringView fbx_filename, StringView src_dir, const ModelMeta* meta, bool ignore_geometry) {
		PROFILE_FUNCTION();
		const u32 c = (u32)m_scene->meshes.count;

		Array<const ufbx_material*> materials(m_allocator);
		HashMap<String, bool> names(m_allocator);
		names.reserve(c);
		m_meshes.reserve(c);

		HashMap<GeomPartition, i32> geom_map(m_allocator);
		geom_map.reserve(c);

		for (const ufbx_node* fbx_mesh : m_scene->nodes) {
			const ufbx_mesh* geom = fbx_mesh->mesh;
			if (!geom) continue;
			if (geom->num_vertices == 0) continue;

			// per-instance materials, same indices as geom->materials
			const ufbx_material_list& mesh_materials = fbx_mesh->materials.count > 0 ? fbx_mesh->materials : geom->materials;
			const u32 mat_count = (u32)mesh_materials.count;

			bool is_skinned = meta && meta->force_skin;
			const ufbx_skin_deformer* skin = getSkin(geom);
			if (skin) {
				for (const ufbx_skin_cluster* cluster : skin->clusters) {
					// cluster weights are not loaded if geometry is ignored
					if (m_scene->metadata.geometry_ignored ? cluster->bone_node != nullptr : cluster->num_weights > 0) {
						is_skinned = true;
						break;
					}
				}
			}

			// mesh can have materials, which are not used by any triangles
			// we don't want to create empty meshes for them
			for (u32 part_idx = 0; part_idx < geom->material_parts.count; ++part_idx) {
				const ufbx_mesh_part& part = geom->material_parts.data[part_idx];
				if (part.num_triangles == 0) continue;

				const u32 fbx_mat_index = part.index;
				const ufbx_material* fbx_mat = fbx_mat_index < mat_count ? mesh_materials.data[fbx_mat_index] : nullptr;

				ImportMesh& mesh = m_meshes.emplace(m_allocator);
				mesh.mesh_index = m_meshes.size() - 1;
				const ufbx_node* parent = getParentNode(fbx_mesh);
				mesh.parent_id = parent ? u64(parent) : 0;
				m_fbx_meshes.push(fbx_mesh);

				i32 mat_idx = materials.indexOf(fbx_mat);
				if (mat_idx < 0) {
					mat_idx = materials.size();
					ImportMaterial& mat = m_materials.emplace(m_allocator);
					ufbx_vec3 diffuse_color = fbx_mat ? fbx_mat->pbr.base_color.value_vec3 : ufbx_vec3{1, 1, 1};
					if (fbx_mat && fbx_mat->pbr.base_factor.has_value) {
						const ufbx_real factor = fbx_mat->pbr.base_factor.value_real;
						diffuse_color.x *= factor;
						diffuse_color.y *= factor;
						diffuse_color.z *= factor;
					}
					mat.diffuse_color = { powf((float)diffuse_color.x, 2.2f), powf((float)diffuse_color.y, 2.2f), powf((float)diffuse_color.z, 2.2f) };
					materials.push(fbx_mat);
				}

				const Matrix geometry_matrix = toLumix(fbx_mesh->geometry_to_node);
				mesh.matrix = toLumix(fbx_mesh->node_to_world) * geometry_matrix;
				const bool flip_handness = mesh.matrix.determinant() < 0;

				auto init_geom = [&](ImportGeometry& import_geom, bool skinned) {
					import_geom.flip_handness = flip_handness;
					import_geom.is_skinned = skinned;
					import_geom.material_index = mat_idx;
					import_geom.submesh = mat_count > 1 ? i32(fbx_mat_index) : -1;
					FBXImportGeometry* fbx_geom = new (NewPlaceholder(), import_geom.user_data) FBXImportGeometry;
					fbx_geom->geom = geom;
					fbx_geom->mesh = fbx_mesh;
					fbx_geom->part = part_idx;
				};

				if (is_skinned) {
					ImportGeometry& import_geom = m_geometries.emplace(m_allocator);
					mesh.geometry_idx = m_geometries.size() - 1;
					init_geom(import_geom, true);
				}
				else {
					const GeomPartition match = {
						.geom = geom,
						.partition = part_idx,
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
						mesh.geometry_idx = m_geometries.size() - 1;
						init_geom(import_geom, false);
					}
				}

				getImportMeshName(mesh, fbx_mesh, names, mat_count > 1 ? i32(fbx_mat_index) : -1);
				m_geometries[mesh.geometry_idx].name = mesh.name; // TODO name from ufbx_mesh
				mesh.lod = detectMeshLOD(mesh.name);
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

		// gather textures
		FileSystem& filesystem = m_app.getEngine().getFileSystem();
		for (u32 i = 0, num_mats = (u32)m_materials.size(); i < num_mats; ++i) {
			ImportMaterial& mat = m_materials[i];
			const ufbx_material* fbx_mat = materials[i];
			if (!fbx_mat) continue;

			auto gatherTexture = [&](ImportTexture::Type type, const ufbx_material_map& map) {
				const ufbx_texture* texture = map.texture;
				if (!texture) return;

				ImportTexture& tex = mat.textures[type];
				ufbx_string filename = texture->relative_filename;
				if (filename.length == 0) filename = texture->absolute_filename;
				tex.path = toStringView(filename);
			};

			gatherTexture(ImportTexture::DIFFUSE, fbx_mat->pbr.base_color);
			gatherTexture(ImportTexture::NORMAL, fbx_mat->pbr.normal_map);
			gatherTexture(ImportTexture::SPECULAR, fbx_mat->fbx.specular_color);
		}
	}

	void extractEmbedded(const ufbx_scene& m_scene, StringView src_dir, IAllocator& allocator) const
	{
		PROFILE_FUNCTION();
		for (const ufbx_texture_file& tex_file : m_scene.texture_files) {
			if (tex_file.content.size == 0) continue;

			ufbx_string name = tex_file.relative_filename;
			if (name.length == 0) name = tex_file.absolute_filename;
			if (name.length == 0) continue;

			StringView filename = toStringView(name);
			const PathInfo pi(filename);
			const StaticString<MAX_PATH> fullpath(src_dir, pi.basename, ".", pi.extension);

			FileSystem& fs = m_app.getEngine().getFileSystem();
			if (fs.fileExists(fullpath)) continue;

			os::OutputFile file;
			if (!fs.open(fullpath, file)) {
				logError("Failed to save ", fullpath);
				return;
			}

			if (!file.write(tex_file.content.data, tex_file.content.size)) {
				logError("Failed to write ", fullpath);
			}
			file.close();
		}
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

		{
			PROFILE_BLOCK("ufbx_load_memory");
			UfbxJobPool pool;
			ufbx_load_opts opts = {};
			opts.thread_opts.pool.run_fn = &UfbxJobPool::run;
			opts.thread_opts.pool.wait_fn = &UfbxJobPool::wait;
			opts.thread_opts.pool.user = &pool;
			// simple parsing does not need vertex data, but it needs to know which materials are used by any
			// face, `keep_face_materials` is our patch of ufbx, it keeps face indices and materials
			opts.ignore_geometry = ignore_geometry;
			opts.keep_face_materials = ignore_geometry;
			opts.ignore_embedded = ignore_geometry;
			opts.skip_skin_vertices = ignore_geometry;
			opts.target_axes = ufbx_axes_right_handed_y_up;
			opts.target_unit_meters = meta && meta->scene_scale > 0 ? 1.0 / meta->scene_scale : 1.0;
			opts.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
			ufbx_error error;
			m_scene = ufbx_load_memory(data.data(), data.size(), &opts, &error);
			if (m_scene && ignore_geometry && m_scene->metadata.version < 6000) {
				// legacy FBX files are not supported by `keep_face_materials`, load everything
				ufbx_free_scene(m_scene);
				opts.ignore_geometry = false;
				opts.keep_face_materials = false;
				m_scene = ufbx_load_memory(data.data(), data.size(), &opts, &error);
			}
			if (!m_scene) {
				logError("Failed to import \"", filename, ": ", error.description.data, "\n"
					"Please try to convert the FBX file with Autodesk FBX Converter or some other software to the latest version.");
				return false;
			}
		}
		StringView src_dir = Path::getDir(filename);
		if (!ignore_geometry) extractEmbedded(*m_scene, src_dir, m_allocator);

		gatherObjects();
		gatherMeshes(filename, src_dir, meta, ignore_geometry);
		if(!meta || !meta->ignore_animations) gatherAnimations(filename);
		if (meta) gatherLights(*meta);

		if (!ignore_geometry) {
			bool any_skinned = false;
			for (const ImportGeometry& g : m_geometries) any_skinned = any_skinned || g.is_skinned;
			// TODO why do we need this here?
			gatherBones(meta->force_skin || any_skinned);
		}

		if (m_bones.empty() && m_meshes.empty() && m_objects.empty() && m_animations.empty()) {
			logError(filename, ": found nothing to import");
			return false;
		}

		if (meta) postprocess(*meta, filename);

		return true;
	}

	IAllocator& m_allocator;
	Array<const ufbx_node*> m_fbx_meshes;
	ufbx_scene* m_scene;
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
