#pragma once


#include "engine/array.h"
#include "engine/flag_set.h"
#include "engine/geometry.h"
#include "engine/hash_map.h"
#include "engine/math.h"
#include "engine/string.h"
#include "engine/math.h"
#include "engine/resource.h"
#include "ffr/ffr.h"
#include "renderer.h"


struct lua_State;


namespace Lumix
{

class Material;
struct Mesh;
class Model;
struct Pose;
class Renderer;
class InputMemoryStream;


struct LUMIX_RENDERER_API RayCastModelHit
{
	bool is_hit;
	float t;
	DVec3 origin;
	Vec3 dir;
	Mesh* mesh;
	EntityPtr entity;
	ComponentType component_type;
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

		INSTANCE0,
		INSTANCE1,
		INSTANCE2,

		COUNT,

		NONE = 0xff
	};

	struct RenderData
	{
		ffr::VertexDecl vertex_decl;
		AttributeSemantic attributes_semantic[ffr::VertexDecl::MAX_ATTRIBUTES];
		ffr::BufferHandle vertex_buffer_handle;
		u32 vb_stride; 
		ffr::BufferHandle index_buffer_handle;
		ffr::DataType index_type;
		int indices_count;
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
		u8 vb_stride,
		const char* name,
		const AttributeSemantic* semantics,
		Renderer& renderer,
		IAllocator& allocator);

	void setMaterial(Material* material, Model& model, Renderer& renderer);
	bool areIndices16() const { return flags.isSet(Flags::INDICES_16_BIT); }

	Type type;
	Array<u8> indices;
	Array<Vec3> vertices;
	Array<Skin> skin;
	FlagSet<Flags, u8> flags;
	u32 sort_key;
	u8 layer;
	String name;
	Material* material;
	RenderData* render_data;
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
	RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Pose* pose);
	const AABB& getAABB() const { return m_aabb; }
	LOD* getLODs() { return m_lods; }
	void onBeforeReady() override;
	bool isSkinned() const;

	static void registerLuaAPI(lua_State* L);

public:
	static const u32 FILE_MAGIC = 0x5f4c4d4f; // == '_LM2'
	static const int MAX_LOD_COUNT = 4;

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
	LOD m_lods[MAX_LOD_COUNT];
	float m_bounding_radius;
	BoneMap m_bone_map;
	AABB m_aabb;
	int m_first_nonroot_bone_index;
};


} // namespace Lumix
