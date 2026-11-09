#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/geometry.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/math.h"
#include "core/stream.h"
#include "core/tag_allocator.h"

#include "engine/resource.h"
#include "gpu/gpu.h"
#include "vertex_decl.h"


namespace Lumix {

struct Material;
struct Mesh;
struct Model;
struct Pose;
struct Renderer;
template <typename T> struct Delegate;

enum class AttributeSemantic : u8 {
	POSITION,
	NORMAL,
	TANGENT,
	BITANGENT,
	COLOR0,
	COLOR1,
	JOINTS,
	WEIGHTS,
	TEXCOORD0,
	TEXCOORD1,
	AO,

	COUNT,

	NONE = 0xff
};

//@ struct
struct LUMIX_RENDERER_API RayCastModelHit {
	bool is_hit;
	float t;
	DVec3 origin;
	Vec3 dir;
	Mesh* mesh;
	EntityPtr entity;
	ComponentType component_type;
	u32 subindex;
	using Filter = Delegate<bool (const RayCastModelHit&)>;
};

enum class MaterialIndex : u32 {}; // strong-typed handle

struct MeshMaterial {
	enum Flags : u8 {
		NONE = 0,
		OWN_MATERIAL_INDEX = 1 << 0,
	};
	Material* material;
	u32 sort_key;
	MaterialIndex material_index;
	Flags flags;
};

struct SOATransform {
	float* px = nullptr;
	float* py = nullptr;
	float* pz = nullptr;
	float* rx = nullptr;
	float* ry = nullptr;
	float* rz = nullptr;
	float* rw = nullptr;
};

struct LUMIX_RENDERER_API Mesh {
	struct Skin {
		Vec4 weights;
		i16 indices[4];
	};

	enum Type : u8 {
		RIGID,
		SKINNED,

		LAST_TYPE
	};

	enum Flags : u8 {
		NONE = 0,
		INDICES_16_BIT = 1 << 0
	};

	Mesh(const VertexDecl& vertex_decl,
		u8 vb_stride,
		StringView name,
		const AttributeSemantic* semantics,
		Renderer& renderer,
		IAllocator& allocator);
	Mesh(Mesh&& rhs);
	
	void operator=(Mesh&&) = delete;

	bool areIndices16() const { return flags & Flags::INDICES_16_BIT; }

	Type type;
	OutputMemoryStream indices;
	Array<Vec3> vertices;
	Array<Skin> skin;
	Flags flags = Flags::NONE;
	String name;
	VertexDecl vertex_decl;
	gpu::PrimitiveType primitive_type = gpu::PrimitiveType::TRIANGLES;
	AttributeSemantic attributes_semantic[VertexDecl::MAX_ATTRIBUTES];
	// defines with the layout of vertices, see vertex_pulling.hlsli
	const char* semantics_defines = "";

	// the only per draw data of vertex pulling, a uniform buffer
	struct VertexBufferBinding {
		u32 vertex_buffer;		// bindless handle
		u32 instance_buffer;	// bindless handle, instance data pulled by shaders (DYNAMIC variant), 0 if not used
		u32 instance_offset;	// in bytes
		u32 padding;
	};
	Renderer& renderer;
	float lod = 0;

	gpu::BufferHandle vertex_buffer_handle;
	u32 vb_stride;
	gpu::BufferHandle index_buffer_handle;
	gpu::DataType index_type;
	u32 indices_count;

	// cluster DAG for GPU driven LOD, see mesh_clusters.h, baked by the model importer
	// Meshes with clusters have no vertices and indices of their own: vertex_buffer_handle is one buffer with the headers of clusters followed by their
	// bit packed vertices (position and then attributes, interleaved, see mesh_clusters.h; vb_stride and the vertex decl only say which attributes there are),
	// and cluster_index_buffer has u16 indices local to a cluster,
	// cluster `c` starts at c * cluster_index_stride, so that all selected clusters of a mesh are drawn by one instanced draw.
	// The CPU `vertices` and `indices` are made of the finest clusters.
	gpu::BufferHandle cluster_buffer = gpu::INVALID_BUFFER;
	gpu::BufferHandle cluster_index_buffer = gpu::INVALID_BUFFER;
	u32 cluster_count = 0;
	u32 cluster_index_stride = 0; // indices of every cluster in cluster_index_buffer are padded (with degenerate triangles) to this count, see Model::loadClusters
	gpu::BufferHandle node_buffer = gpu::INVALID_BUFFER; // MeshNodeGPU[node_count], then root_count u32 indices of roots, see MeshClusters
	u32 node_count = 0;
	u32 root_count = 0;
	u32 tree_depth = 0;			// levels of the deepest tree, a leaf node is the last one
	u32 leaf_cluster_count = 0; // the finest clusters, no selection has more clusters than that
};

struct LODMeshIndices
{
	int from;
	int to;
};

//@ object
struct LUMIX_RENDERER_API Model final : Resource {
	using BoneMap = HashMap<BoneNameHash, int>;

	enum class FileVersion : u32 {
		ROOT_MOTION_BONE,
		CLUSTERS, // baked cluster DAGs of the first LOD, see parseClusters
		// 2 was LATEST while models were written with clusters but without stride, keep it that way
		STRIDE = 3, // vertex stride of each mesh is stored, it is padded to 4 bytes
		CLUSTER_CONES = 4, // clusters have a normal cone (MeshClusterGPU is 80 bytes)
		CLUSTER_NODES = 5, // a tree of nodes over the clusters of each mesh, see MeshClusters
		CLUSTER_ATTRIBUTE_ERRORS = 6, // clusters (96 bytes) and nodes (64 bytes) have the error of attributes apart from the error of positions
		CLUSTER_VERTICES = 7, // clusters of the first LOD have their own, packed vertices and u16 indices, the meshes have no vertices of their own, see parseClusters

		CLUSTER_INTERLEAVED = 9, // clusters have one buffer of headers and interleaved, packed vertices and everything else ready to be uploaded, see parseClusters

		LATEST // keep this last
	};
	
#pragma pack(1)
	struct FileHeader {
		static constexpr u32 MAGIC = 0x5f4c4d4f;

		u32 magic = FileHeader::MAGIC;
		FileVersion version = FileVersion::LATEST;
	};
#pragma pack()

	struct Bone {
		enum { MAX_COUNT = 196 };

		explicit Bone(IAllocator& allocator)
			: name(allocator)
		{
		}

		String name;
		LocalRigidTransform transform;
		LocalRigidTransform relative_transform;
	};

	static const ResourceType TYPE;

	Model(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	u32 getLODMeshIndices(float squared_distance) const {
		if (squared_distance < m_lod_distances[0]) return 0;
		if (squared_distance < m_lod_distances[1]) return 1;
		if (squared_distance < m_lod_distances[2]) return 2;
		if (squared_distance < m_lod_distances[3]) return 3;
		return 4;
	}

	Mesh& getMesh(u32 index) { return m_meshes[index]; }
	const Mesh& getMesh(u32 index) const { return m_meshes[index]; }
	Span<MeshMaterial> getMeshMaterials() { return m_mesh_material; }
	MeshMaterial& getMeshMaterial(u32 index) { return m_mesh_material[index]; }
	const MeshMaterial& getMeshMaterial(u32 index) const { return m_mesh_material[index]; }
	int getMeshCount() const { return m_meshes.size(); }
	i16 getBoneParent(u32 idx) const { return m_parents[idx]; }
	const Bone& getBone(u32 i) const { return m_bones[i]; }
	Span<const Bone> getBones() const { return m_bones; }
	int getFirstNonrootBoneIndex() const { return m_first_nonroot_bone_index; }
	BoneMap::ConstIterator getBoneIndex(BoneNameHash hash) const { return m_bone_map.find(hash); }
	void getPose(Pose& pose);
	void getRelativePose(Pose& pose);
	//@ function
	float getOriginBoundingRadius() const { return m_origin_bounding_radius; }
	//@ function
	float getCenterBoundingRadius() const { return m_center_bounding_radius; }
	//@ function
	const AABB& getAABB() const { return m_aabb; }
	RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Pose* pose, EntityPtr entity, const RayCastModelHit::Filter* filter);
	void onBeforeReady() override;
	const float* getLODDistances() const { return m_lod_distances; }
	float* getLODDistances() { return m_lod_distances; }
	const LODMeshIndices* getLODIndices() const { return m_lod_indices; }
	Vec3 evalVertexPose(const Pose& pose, u32 mesh, u32 index) const;
	BoneNameHash getRootMotionBone() const { return m_root_motion_bone; }
	const SOATransform& getInverseBindPose() { return m_inverse_bind; }
	LocalRigidTransform getInverseBindTransform(i32 bone_idx) const;
	Span<const i16> getParents() const { return m_parents; }
	// true if all meshes of the first LOD have cluster DAGs, see Mesh::cluster_buffer
	bool hasClusters() const { return m_has_clusters; }
	// index of the last mesh that is rendered when using clusters, further meshes are lower LODs which clusters replace
	i32 getClusterMeshesEnd() const { return m_lod_indices[0].to >= 0 ? m_lod_indices[0].to : (i32)m_meshes.size() - 1; }

	static constexpr u32 MAX_LOD_COUNT = 4;

private:
	Model(const Model&);
	void operator=(const Model&);

	bool parseBones(InputMemoryStream& file);
	bool parseMeshes(InputMemoryStream& file, FileVersion version);
	bool parseLODs(InputMemoryStream& file);
	bool parseClusters(InputMemoryStream& file, FileVersion version);
	void destroyClusters();
	int getBoneIdx(const char* name);

	void unload() override;
	bool load(Span<const u8> mem) override;

	TagAllocator m_allocator;
	Renderer& m_renderer;
	Array<Mesh> m_meshes;
	Array<MeshMaterial> m_mesh_material;
	Array<Bone> m_bones;

	SOATransform m_inverse_bind;
	Array<i16> m_parents;
	LODMeshIndices m_lod_indices[MAX_LOD_COUNT + 1];
	float m_lod_distances[MAX_LOD_COUNT];
	float m_origin_bounding_radius = 0;
	float m_center_bounding_radius = 0;
	BoneMap m_bone_map;
	AABB m_aabb;
	BoneNameHash m_root_motion_bone;
	int m_first_nonroot_bone_index;
	bool m_has_clusters = false;
};


} // namespace Lumix
