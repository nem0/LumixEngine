#pragma once


#include "core/array.h"
#include "core/geometry.h"
#include "core/hash_map.h"
#include "core/math.h"
#include "core/path.h"
#include "core/stream.h"
#include "core/string.h"
#include "openfbx/ofbx.h"

namespace Lumix {

struct ImpostorTexturesContext {
	virtual ~ImpostorTexturesContext() {}
	virtual void readCallback0(Span<const u8> data) = 0;
	virtual void readCallback1(Span<const u8> data) = 0;
	virtual void readCallback2(Span<const u8> data) = 0;
	virtual void readCallback3(Span<const u8> data) = 0;
	virtual void start() = 0;

	IVec2 tile_size;
	Path path;
};

struct FBXImporter {
	struct VertexLayout {
		u32 size;
		u32 normal_offset;
		u32 uv_offset;
		u32 tangent_offset;
	};

	struct ImportConfig {
		struct Clip {
			StaticString<64> name;
			u32 from_frame;
			u32 to_frame;
		};

		enum class Origin : i32 {
			SOURCE, // keep vertex data as is
			CENTER, // center all meshes as a group
			BOTTOM, // same as center, but don't change Y coordinate
			
			CENTER_EACH_MESH // center each mesh in fbx separately, when exporting each mesh as a subresources
		};

		enum class Physics : i32 {
			NONE,
			CONVEX,
			TRIMESH
		};

		float mesh_scale;
		Origin origin = Origin::SOURCE;
		bool create_impostor = false;
		bool force_normal_recompute = false;
		bool mikktspace_tangents = false;
		bool import_vertex_colors = true;
		bool vertex_color_is_ao = false;
		bool bake_vertex_ao = false;
		bool use_specular_as_roughness = true;
		bool use_specular_as_metallic = false;
		float min_bake_vertex_ao = 0.f;
		Physics physics = Physics::NONE;
		u32 lod_count = 1;
		float lods_distances[4] = {-10, -100, -1000, -10000};
		float autolod_coefs[4] = { 0.75f, 0.5f, 0.25f, 0.125f };
		u8 autolod_mask = 0;
		float bounding_scale = 1.f;
		Span<const Clip> clips;
		u32 animation_flags = 0;
		float anim_translation_error = 1.f;
		float anim_rotation_error = 1.f;
		Path skeleton;
		BoneNameHash root_motion_bone;
	};

	enum class Orientation {
		Y_UP,
		Z_UP,
		Z_MINUS_UP,
		X_MINUS_UP,
		X_UP
	};

	struct Key {
		Vec3 pos;
		Quat rot;
	};

	struct Skin {
		float weights[4];
		i16 joints[4];
		int count = 0;
	};

	struct ImportAnimation {
		const ofbx::AnimationStack* fbx = nullptr;
		const ofbx::IScene* scene = nullptr;
		StringView name;
	};

	struct ImportTexture {
		enum Type {
			DIFFUSE,
			NORMAL,
			SPECULAR,
			COUNT
		};

		const ofbx::Texture* fbx = nullptr;
		bool import = true;
		bool to_dds = true;
		bool is_valid = false;
		StringView path;
		StaticString<MAX_PATH> src;
	};

	struct ImportMaterial {
		const ofbx::Material* fbx = nullptr;
		bool import = true;
		bool alpha_cutout = false;
		ImportTexture textures[ImportTexture::COUNT];
		char shader[20];
	};

	struct ImportMesh {
		ImportMesh(IAllocator& allocator)
			: vertex_data(allocator)
			, indices(allocator)
		{
		}

		const ofbx::Mesh* fbx = nullptr;
		const ofbx::Material* fbx_mat = nullptr;
		bool is_skinned = false;
		int bone_idx = -1;
		bool import = true;
		u32 lod = 0;
		int submesh = -1;
		OutputMemoryStream vertex_data;
		Array<u32> indices;
		Local<Array<u32>> autolod_indices[4];
		AABB aabb;
		float origin_radius_squared;
		Vec3 origin = Vec3(0);
	};

	enum class ReadFlags : u32 {
		NONE = 0,
		FORCE_SKINNED = 1 << 0,
		IGNORE_GEOMETRY = 1 << 1,
		IGNORE_MATERIALS = 1 << 2,
	};

	FBXImporter(struct StudioApp& app);
	~FBXImporter();
	void init();
	bool setSource(const Path& filename, ReadFlags flags);
	bool writeMaterials(const Path& src, const ImportConfig& cfg, bool force);
	bool writeAnimations(const Path& src, const ImportConfig& cfg);
	bool writeSubmodels(const Path& src, const ImportConfig& cfg);
	bool writePrefab(const Path& src, const ImportConfig& cfg, bool split_meshes);
	bool writeModel(const Path& src, const ImportConfig& cfg);
	bool writePhysics(const Path& src, const ImportConfig& cfg, bool split_meshes);
	void createImpostorTextures(struct Model* model, ImpostorTexturesContext& ctx, bool bake_normals);

	u32 getBoneCount() const { return (u32)m_bones.size(); }
	const Array<ImportMesh>& getMeshes() const { return m_meshes; }
	const Array<ImportAnimation>& getAnimations() const { return m_animations; }

	static void getImportMeshName(const ImportMesh& mesh, char (&name)[256]);
	ofbx::IScene* getOFBXScene() { return m_scene; }

private:
	void createAutoLODs(const ImportConfig& cfg, ImportMesh& import_mesh);
	bool findTexture(StringView src_dir, StringView ext, FBXImporter::ImportTexture& tex) const;
	const ImportMesh* getAnyMeshFromBone(const ofbx::Object* node, int bone_idx) const;
	void gatherMaterials(StringView fbx_filename, StringView src_dir);

	void sortBones(bool force_skinned);
	void gatherBones(bool force_skinned);
	void gatherAnimations();
	void postprocessMeshes(const ImportConfig& cfg, const Path& path);
	void gatherMeshes();
	void insertHierarchy(Array<const ofbx::Object*>& bones, const ofbx::Object* node);
	
	template <typename T> void write(const T& obj) { m_out_file.write(&obj, sizeof(obj)); }
	void write(const void* ptr, size_t size) { m_out_file.write(ptr, size); }
	void writeString(const char* str);
	void fillSkinInfo(Array<Skin>& skinning, const ImportMesh& mesh) const;
	Vec3 fixOrientation(const Vec3& v) const;
	Quat fixOrientation(const Quat& v) const;
	void writeImpostorVertices(float center_y, Vec2 bounding_cylinder);
	void writeGeometry(const ImportConfig& cfg);
	void writeGeometry(int mesh_idx, const ImportConfig& cfg);
	void writeImpostorMesh(StringView dir, StringView model_name);
	void writeMeshes(const Path& src, int mesh_idx, const ImportConfig& cfg);
	void writeSkeleton(const ImportConfig& cfg);
	void writeLODs(const ImportConfig& cfg);
	int getAttributeCount(const ImportMesh& mesh, const ImportConfig& cfg) const;
	bool areIndices16Bit(const ImportMesh& mesh, const ImportConfig& cfg) const;
	void writeModelHeader();
	void bakeVertexAO(const ImportConfig& cfg);
	void remap(const OutputMemoryStream& unindexed_triangles, ImportMesh& mesh, u32 vertex_size, const ImportConfig& cfg) const;
	void triangulate(OutputMemoryStream& unindexed_triangles
		, ImportMesh& mesh
		, const ofbx::GeometryPartition& partition
		, u32 vertex_size
		, const Array<FBXImporter::Skin>& skinning
		, const ImportConfig& cfg
		, const Matrix& matrix
		, Array<i32>& tri_indices);

	IAllocator& m_allocator;
	struct FileSystem& m_filesystem;
	StudioApp& m_app;
	struct Shader* m_impostor_shadow_shader = nullptr;
	struct AssetCompiler& m_compiler;
	Array<ImportMaterial> m_materials;
	Array<ImportMesh> m_meshes;
	HashMap<const ofbx::Material*, String> m_material_name_map;
	Array<ImportAnimation> m_animations;
	Array<const ofbx::Object*> m_bones;
	Array<Matrix> m_bind_pose;
	ofbx::IScene* m_scene;
	OutputMemoryStream m_out_file;
	float m_time_scale = 1.0f;
	float m_fbx_scale = 1.f;
	Orientation m_orientation = Orientation::Y_UP;
};


} // namespace Lumix