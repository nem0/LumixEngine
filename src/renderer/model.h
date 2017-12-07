#pragma once


#include "engine/array.h"
#include "engine/flag_set.h"
#include "engine/geometry.h"
#include "engine/hash_map.h"
#include "engine/matrix.h"
#include "engine/string.h"
#include "engine/vec.h"
#include "engine/resource.h"
#include <bgfx/bgfx.h>


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
	ComponentHandle m_component;
	Entity m_entity;
	ComponentType m_component_type;
};


struct LUMIX_RENDERER_API Mesh
{
	struct Skin
	{
		Vec4 weights;
		i16 indices[4];
	};

	enum Type : u8
	{
		RIGID_INSTANCED,
		SKINNED,
		MULTILAYER_RIGID,
		MULTILAYER_SKINNED,
		RIGID,
	};

	enum Flags : u8
	{
		INDICES_16_BIT = 1 << 0
	};

	Mesh(Material* mat,
		const bgfx::VertexDecl& vertex_decl,
		const char* name,
		IAllocator& allocator);

	void set(const Mesh& rhs);

	void setMaterial(Material* material, Model& model, Renderer& renderer);

	bool areIndices16() const { return flags.isSet(Flags::INDICES_16_BIT); }

	Type type;
	Array<u8> indices;
	Array<Vec3> vertices;
	Array<Vec2> uvs;
	Array<Skin> skin;
	FlagSet<Flags, u8> flags;
	u64 layer_mask;
	i32 instance_idx;
	int indices_count;
	bgfx::VertexDecl vertex_decl;
	bgfx::VertexBufferHandle vertex_buffer_handle = BGFX_INVALID_HANDLE;
	bgfx::IndexBufferHandle index_buffer_handle = BGFX_INVALID_HANDLE;
	string name;
	Material* material;
};


struct LODMeshIndices
{
	int from;
	int to;
};


class LUMIX_RENDERER_API Model LUMIX_FINAL : public Resource
{
public:
	typedef HashMap<u32, int> BoneMap;

	enum class Attrs
	{
		Position,
		Normal,
		Tangent,
		Bitangent,
		Color0,
		Color1,
		Indices,
		Weight,
		TexCoord0,
		TexCoord1,
		TexCoord2,
		TexCoord3,
		TexCoord4,
		TexCoord5,
		TexCoord6,
		TexCoord7,
	};

#pragma pack(1)
	struct FileHeader
	{
		u32 magic;
		u32 version;
	};
#pragma pack()

	enum class FileVersion : u32
	{
		FIRST,
		WITH_FLAGS,
		SINGLE_VERTEX_DECL,
		BOUNDING_SHAPES_PRECOMPUTED,
		MULTIPLE_VERTEX_DECLS,

		LATEST // keep this last
	};

	enum class LoadingFlags : u32
	{
		KEEP_SKIN = 1 << 0
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

public:
	Model(const Path& path, ResourceManagerBase& resource_manager, Renderer& renderer, IAllocator& allocator);
	~Model();

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
	void setKeepSkin();
	void onBeforeReady() override;

	static void registerLuaAPI(lua_State* L);

public:
	static const u32 FILE_MAGIC = 0x5f4c4d4f; // == '_LMO'
	static const int MAX_LOD_COUNT = 4;
	static bool force_keep_skin;

private:
	Model(const Model&);
	void operator=(const Model&);

	bool parseVertexDecl(FS::IFile& file, bgfx::VertexDecl* vertex_decl);
	bool parseVertexDeclEx(FS::IFile& file, bgfx::VertexDecl* vertex_decl);
	bool parseBones(FS::IFile& file);
	bool parseMeshes(const bgfx::VertexDecl& global_vertex_decl, FS::IFile& file, FileVersion version, u32 global_flags);
	bool parseMeshesOld(bgfx::VertexDecl global_vertex_decl, FS::IFile& file, FileVersion version, u32 global_flags);
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
