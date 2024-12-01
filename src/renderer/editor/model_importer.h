#pragma once

#include "core/array.h"
#include "core/geometry.h"
#include "core/math.h"
#include "core/path.h"
#include "core/span.h"
#include "core/stream.h"
#include "engine/lumix.h"
#include "renderer/gpu/gpu.h"

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

enum class AttributeSemantic : u8;

// Base class for all model importers
// To implement new importer:
// * inherit from this class
// * see comments next to members, which must be filled by importer (e.g. m_bones)
// TODO finish this comment
struct ModelImporter {
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

		float mesh_scale = 1.f;
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

	struct Key {
		Vec3 pos;
		Quat rot;
	};

	struct ImportTexture {
		enum Type {
			DIFFUSE,
			NORMAL,
			SPECULAR,
			COUNT
		};

		bool import = true;
		StringView path;
		StaticString<MAX_PATH> src;
	};

	struct ImportAnimation {
		u32 index = 0xffFFffFF;
		StringView name;
		double length;
		float fps;
	};

	struct ImportMaterial {
		ImportMaterial(IAllocator& allocator) : name(allocator) {}
		ImportTexture textures[ImportTexture::COUNT];
		Vec3 diffuse_color;
		String name;
	};

	struct AttributeDesc {
		AttributeSemantic semantic;
		gpu::AttributeType type;
		u8 num_components;
	};

	struct ImportMesh {
		ImportMesh(IAllocator& allocator)
			: vertex_data(allocator)
			, indices(allocator)
			, name(allocator)
			, attributes(allocator)
		{
		}

		u32 mesh_index = 0xFFffFFff;
		u32 material_index = 0xffFFffFF;
		bool is_skinned = false;
		int bone_idx = -1;
		u32 lod = 0;
		int submesh = -1;
		OutputMemoryStream vertex_data;
		u32 vertex_size = 0xffFFffFF;
		Array<AttributeDesc> attributes;
		Array<u32> indices;
		u32 index_size;
		Local<Array<u32>> autolod_indices[4];
		AABB aabb;
		float origin_radius_squared;
		Vec3 origin = Vec3(0);
		String name;
	};

	struct Bone {
		Bone(IAllocator& allocator) : name(allocator) {}

		bool operator ==(const Bone& rhs) const { return id == rhs.id; }

		u64 parent_id = 0;
		u64 id = 0;
		String name;
		Matrix bind_pose_matrix;
	};

	const Array<ImportMesh>& getMeshes() const { return m_meshes; }
	const Array<ImportAnimation>& getAnimations() const { return m_animations; }

	bool writeModel(const Path& src, const ImportConfig& cfg);
	bool writeAnimations(const Path& src, const ImportConfig& cfg);
	bool writeMaterials(const Path& src, const ImportConfig& cfg, bool force) const;
	bool writePhysics(const Path& src, const ImportConfig& cfg, bool split_meshes);
	void createImpostorTextures(struct Model* model, ImpostorTexturesContext& ctx, bool bake_normals);
	virtual void postprocess(const ImportConfig& cfg, const Path& path) = 0;
	u32 getBoneCount() const { return (u32)m_bones.size(); }

protected:
	ModelImporter(struct StudioApp& app);
	virtual ~ModelImporter() {}
	StudioApp& m_app;
	IAllocator& m_allocator;
	OutputMemoryStream m_out_file;
	struct Shader* m_impostor_shadow_shader = nullptr;
	
	// importers must fill these members
	Array<Bone> m_bones; // parent must be before children
	Array<ImportMaterial> m_materials;
	Array<ImportMesh> m_meshes;
	Array<ImportAnimation> m_animations;
	float m_scene_scale = 1.f;

	template <typename T> void write(const T& obj) { m_out_file.write(&obj, sizeof(obj)); }
	void write(const void* ptr, size_t size) { m_out_file.write(ptr, size); }
	void writeString(const char* str);
	
	virtual void fillTracks(const ImportAnimation& anim
		, Array<Array<Key>>& tracks
		, u32 from_sample
		, u32 num_samples
	) const = 0;

	// TODO cleanup
	void writeModelHeader();
	void writeImpostorVertices(float center_y, Vec2 bounding_cylinder);
	void writeImpostorMesh(StringView dir, StringView model_name);
	void writeMeshes(const Path& src, int mesh_idx, const ImportConfig& cfg);
	void writeLODs(const ImportConfig& cfg);
	void writeGeometry(const ImportConfig& cfg);
	void writeGeometry(int mesh_idx, const ImportConfig& cfg);
	void writeSkeleton(const ImportConfig& cfg);
	bool findTexture(StringView src_dir, StringView ext, ImportTexture& tex) const;
	void bakeVertexAO(const ImportConfig& cfg);
	
	// compute AO, auto LODs, etc.
	void postprocessCommon(const ImportConfig& cfg);
};

} // namespace Lumix