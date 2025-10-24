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

//@ struct name RayCastModelHit
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

	Mesh(const gpu::VertexDecl& vertex_decl,
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
	gpu::VertexDecl vertex_decl;
	AttributeSemantic attributes_semantic[gpu::VertexDecl::MAX_ATTRIBUTES];
	const char* semantics_defines = "";
	Renderer& renderer;
	float lod = 0;

	gpu::BufferHandle vertex_buffer_handle;
	u32 vb_stride;
	gpu::BufferHandle index_buffer_handle;
	gpu::DataType index_type;
	u32 indices_count;
};

struct LODMeshIndices
{
	int from;
	int to;
};

struct LUMIX_RENDERER_API Model final : Resource {
	using BoneMap = HashMap<BoneNameHash, int>;

	enum class FileVersion : u32 {
		ROOT_MOTION_BONE,

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
	int getBoneCount() const { return m_bones.size(); }
	const char* getBoneName(u32 idx) { return m_bones[idx].name.c_str(); }
	i16 getBoneParent(u32 idx) const { return m_parents[idx]; }
	const Bone& getBone(u32 i) const { return m_bones[i]; }
	int getFirstNonrootBoneIndex() const { return m_first_nonroot_bone_index; }
	BoneMap::ConstIterator getBoneIndex(BoneNameHash hash) const { return m_bone_map.find(hash); }
	void getPose(Pose& pose);
	void getRelativePose(Pose& pose);
	float getOriginBoundingRadius() const { return m_origin_bounding_radius; }
	float getCenterBoundingRadius() const { return m_center_bounding_radius; }
	RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Pose* pose, EntityPtr entity, const RayCastModelHit::Filter* filter);
	const AABB& getAABB() const { return m_aabb; }
	void onBeforeReady() override;
	const float* getLODDistances() const { return m_lod_distances; }
	float* getLODDistances() { return m_lod_distances; }
	const LODMeshIndices* getLODIndices() const { return m_lod_indices; }
	Vec3 evalVertexPose(const Pose& pose, u32 mesh, u32 index) const;
	BoneNameHash getRootMotionBone() const { return m_root_motion_bone; }
	const SOATransform& getInverseBindPose() { return m_inverse_bind; }
	LocalRigidTransform getInverseBindTransform(i32 bone_idx) const;
	Span<const i16> getParents() const { return m_parents; }

	static constexpr u32 MAX_LOD_COUNT = 4;

private:
	Model(const Model&);
	void operator=(const Model&);

	bool parseBones(InputMemoryStream& file);
	bool parseMeshes(InputMemoryStream& file, FileVersion version);
	bool parseLODs(InputMemoryStream& file);
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
};


} // namespace Lumix
