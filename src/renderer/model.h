#pragma once


#include "engine/array.h"
#include "engine/flag_set.h"
#include "engine/geometry.h"
#include "engine/hash_map.h"
#include "engine/matrix.h"
#include "engine/string.h"
#include "engine/vec.h"
#include "engine/resource.h"
#include "ffr/ffr.h"
#include "renderer.h"


struct lua_State;


namespace Lumix
{

struct Frustum;
class Material;
struct Mesh;
class Model;
struct Pose;
class Renderer;
class ResourceManager;


namespace FS
{
class FileSystem;
struct IFile;
}


struct LUMIX_RENDERER_API RayCastModelHit
{
	bool m_is_hit;
	float m_t;
	Vec3 m_origin;
	Vec3 m_dir;
	Mesh* m_mesh;
	EntityPtr m_entity;
	ComponentType m_component_type;
};


struct LUMIX_RENDERER_API Mesh
{
	enum class AttributeSemantic : u8
	{
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

		NONE = 0xff
	};

	struct Skin
	{
		Vec4 weights;
		i16 indices[4];
	};

	enum Type : u8
	{
		RIGID_INSTANCED,
		SKINNED,

		LAST_TYPE
	};

	enum Flags : u8
	{
		INDICES_16_BIT = 1 << 0
	};

	Mesh(Material* mat,
		const ffr::VertexDecl& vertex_decl,
		const char* name,
		const AttributeSemantic* semantics,
		IAllocator& allocator);

	void set(const Mesh& rhs);
	void setMaterial(Material* material, Model& model, Renderer& renderer);
	bool areIndices16() const { return flags.isSet(Flags::INDICES_16_BIT); }
	int getAttributeOffset(AttributeSemantic attr) const;
	bool hasAttribute(AttributeSemantic attr) const;

	Type type;
	Array<u8> indices;
	Array<Vec3> vertices;
	Array<Vec2> uvs;
	Array<Skin> skin;
	FlagSet<Flags, u8> flags;
	u64 layer_mask;
	u32 sort_key;
	int indices_count;
	ffr::VertexDecl vertex_decl;
	AttributeSemantic attributes_semantic[ffr::VertexDecl::MAX_ATTRIBUTES];
	ffr::BufferHandle vertex_buffer_handle;
	ffr::BufferHandle index_buffer_handle;
	string name;
	Material* material;

	static u32 s_last_sort_key;
};


struct LODMeshIndices
{
	int from;
	int to;
};


class LUMIX_RENDERER_API Model final : public Resource
{
public:
	typedef HashMap<u32, int> BoneMap;
	
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

	enum class LoadingFlags : u32
	{
		KEEP_SKIN_DEPRECATED = 1 << 0
	};

	struct LOD
	{
		int from_mesh;
		int to_mesh;

		float distance;
	};

	struct Bone
	{
		enum { MAX_COUNT = 196 };

		explicit Bone(IAllocator& allocator)
			: name(allocator)
			, parent(allocator)
		{
		}

		string name;
		string parent;
		RigidTransform transform;
		RigidTransform relative_transform;
		RigidTransform inv_bind_transform;
		int parent_idx;
	};

	static const ResourceType TYPE;

public:
	Model(const Path& path, ResourceManagerBase& resource_manager, Renderer& renderer, IAllocator& allocator);
	~Model();

	ResourceType getType() const override { return TYPE; }

	LODMeshIndices getLODMeshIndices(float squared_distance) const
	{
		int i = 0;
		while (squared_distance >= m_lods[i].distance) ++i;
		return {m_lods[i].from_mesh, m_lods[i].to_mesh};
	}

	Mesh& getMesh(int index) { return m_meshes[index]; }
	const Mesh& getMesh(int index) const { return m_meshes[index]; }
	const Mesh* getMeshPtr(int index) const { return &m_meshes[index]; }
	int getMeshCount() const { return m_meshes.size(); }
	int getBoneCount() const { return m_bones.size(); }
	const Bone& getBone(int i) const { return m_bones[i]; }
	int getFirstNonrootBoneIndex() const { return m_first_nonroot_bone_index; }
	BoneMap::iterator getBoneIndex(u32 hash) { return m_bone_map.find(hash); }
	void getPose(Pose& pose);
	void getRelativePose(Pose& pose);
	float getBoundingRadius() const { return m_bounding_radius; }
	RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform, const Pose* pose);
	const AABB& getAABB() const { return m_aabb; }
	LOD* getLODs() { return m_lods; }
	void onBeforeReady() override;

	static void registerLuaAPI(lua_State* L);

public:
	static const u32 FILE_MAGIC = 0x5f4c4d4f; // == '_LM2'
	static const int MAX_LOD_COUNT = 4;

private:
	Model(const Model&);
	void operator=(const Model&);

	bool parseBones(FS::IFile& file);
	bool parseMeshes(FS::IFile& file, FileVersion version);
	bool parseLODs(FS::IFile& file);
	int getBoneIdx(const char* name);

	void unload() override;
	bool load(FS::IFile& file) override;

private:
	IAllocator& m_allocator;
	Renderer& m_renderer;
	Array<Mesh> m_meshes;
	Array<Bone> m_bones;
	LOD m_lods[MAX_LOD_COUNT];
	float m_bounding_radius;
	BoneMap m_bone_map;
	AABB m_aabb;
	FlagSet<LoadingFlags, u32> m_loading_flags;
	int m_first_nonroot_bone_index;
};


} // namespace Lumix
