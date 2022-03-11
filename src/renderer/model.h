#pragma once


#include "engine/array.h"
#include "engine/flag_set.h"
#include "engine/geometry.h"
#include "engine/hash.h"
#include "engine/hash_map.h"
#include "engine/math.h"
#include "engine/resource.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "gpu/gpu.h"


struct lua_State;


namespace Lumix {

struct Material;
struct Mesh;
struct Model;
struct Pose;
struct Renderer;
template <typename T> struct Delegate;


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


struct LUMIX_RENDERER_API Mesh {
	enum class AttributeSemantic : u8 {
		POSITION,
		NORMAL,
		TANGENT,
		BITANGENT,
		COLOR0,
		COLOR1,
		INDICES,
		WEIGHTS,
		TEXCOORD0,
		TEXCOORD1,
		AO,

		INSTANCE0,
		INSTANCE1,
		INSTANCE2,

		COUNT,

		NONE = 0xff
	};

	struct RenderData {
		gpu::BufferHandle vertex_buffer_handle;
		u32 vb_stride;
		gpu::BufferHandle index_buffer_handle;
		gpu::DataType index_type;
		int indices_count;
	};

	struct Skin {
		Vec4 weights;
		i16 indices[4];
	};

	enum Type : u8 {
		RIGID,
		SKINNED,

		LAST_TYPE
	};

	enum Flags : u8 { INDICES_16_BIT = 1 << 0 };

	Mesh(Material* mat,
		const gpu::VertexDecl& vertex_decl,
		u8 vb_stride,
		const char* name,
		const AttributeSemantic* semantics,
		Renderer& renderer,
		IAllocator& allocator);
	Mesh(Mesh&& rhs);
	~Mesh();

	void operator=(Mesh&&) = delete;

	void setMaterial(Material* material, Model& model, Renderer& renderer);
	bool areIndices16() const { return flags.isSet(Flags::INDICES_16_BIT); }

	Type type;
	OutputMemoryStream indices;
	Array<Vec3> vertices;
	Array<Skin> skin;
	FlagSet<Flags, u8> flags;
	u32 sort_key;
	u8 layer;
	String name;
	Material* material;
	gpu::VertexDecl vertex_decl;
	AttributeSemantic attributes_semantic[gpu::VertexDecl::MAX_ATTRIBUTES];
	RenderData* render_data;
	Renderer& renderer;
	float lod = 0;
};


struct LODMeshIndices
{
	int from;
	int to;
};


struct LUMIX_RENDERER_API Model final : Resource
{
public:
	using BoneMap = HashMap<StableHash, int>;
	
#pragma pack(1)
	struct FileHeader
	{
		u32 magic;
		u32 version;
	};
#pragma pack()

	enum class FileVersion : u32
	{
		LATEST // keep this last
	};

	struct Bone
	{
		enum { MAX_COUNT = 196 };

		explicit Bone(IAllocator& allocator)
			: name(allocator)
			, parent_idx(-1)
		{
		}

		String name;
		LocalRigidTransform transform;
		LocalRigidTransform relative_transform;
		LocalRigidTransform inv_bind_transform;
		int parent_idx;
	};

	static const ResourceType TYPE;

public:
	Model(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);
	~Model();

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
	int getMeshCount() const { return m_meshes.size(); }
	int getBoneCount() const { return m_bones.size(); }
	const char* getBoneName(u32 idx) { return m_bones[idx].name.c_str(); }
	i32 getBoneParent(u32 idx) { return m_bones[idx].parent_idx; }
	const Bone& getBone(u32 i) const { return m_bones[i]; }
	int getFirstNonrootBoneIndex() const { return m_first_nonroot_bone_index; }
	BoneMap::ConstIterator getBoneIndex(StableHash hash) const { return m_bone_map.find(hash); }
	void getPose(Pose& pose);
	void getRelativePose(Pose& pose);
	float getOriginBoundingRadius() const { return m_origin_bounding_radius; }
	float getCenterBoundingRadius() const { return m_center_bounding_radius; }
	RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Pose* pose, EntityPtr entity, const RayCastModelHit::Filter* filter);
	const AABB& getAABB() const { return m_aabb; }
	void onBeforeReady() override;
	bool isSkinned() const;
	const float* getLODDistances() const { return m_lod_distances; }
	float* getLODDistances() { return m_lod_distances; }
	const LODMeshIndices* getLODIndices() const { return m_lod_indices; }

public:
	static const u32 FILE_MAGIC = 0x5f4c4d4f; // == '_LM2'
	static const u32 MAX_LOD_COUNT = 4;

private:
	Model(const Model&);
	void operator=(const Model&);

	bool parseBones(InputMemoryStream& file);
	bool parseMeshes(InputMemoryStream& file, FileVersion version);
	bool parseLODs(InputMemoryStream& file);
	int getBoneIdx(const char* name);

	void unload() override;
	bool load(u64 size, const u8* mem) override;

private:
	IAllocator& m_allocator;
	Renderer& m_renderer;
	Array<Mesh> m_meshes;
	Array<Bone> m_bones;
	LODMeshIndices m_lod_indices[MAX_LOD_COUNT + 1];
	float m_lod_distances[MAX_LOD_COUNT];
	float m_origin_bounding_radius = 0;
	float m_center_bounding_radius = 0;
	BoneMap m_bone_map;
	AABB m_aabb;
	int m_first_nonroot_bone_index;
};


} // namespace Lumix
