#pragma once

#include "core/array.h"
#include "core/geometry.h"
#include "core/math.h"
#include "core/path.h"
#include "core/stream.h"
#include "renderer/gpu/gpu.h"

namespace Lumix {

struct ImpostorTexturesContext;
struct ModelMeta;

enum class AttributeSemantic : u8;

// Base class for model importers
// To add a new importer, derive from this class and implement all pure virtual functions
struct ModelImporter {
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
			: vertex_buffer(allocator)
			, indices(allocator)
			, name(allocator)
			, attributes(allocator)
		{
		}

		u32 mesh_index = 0xFFffFFff;
		u32 material_index = 0xffFFffFF;
		bool is_skinned = false;
		i32 bone_idx = -1;
		u32 lod = 0;
		i32 submesh = -1;
		OutputMemoryStream vertex_buffer;
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

	void init(); // TODO get rid of this?

	// simple parsing - you can get only the list of objects (mesh, materials, animations, etc.),
	// but not their content (geometry, embedded textures, etc.)
	// calling write(...) after this is invalid
	// used for to get a list of subresources (addSubresources) and to reimport materials
	virtual bool parseSimple(const Path& filename) = 0;
	
	// full parsing - parse all data, including geometry
	virtual bool parse(const Path& filename, const ModelMeta& meta) = 0;
	
	// meta must be the same as in parse
	// TODO fix this (remove meta from these functions?)
	bool write(const Path& src, const ModelMeta& meta);
	bool writeMaterials(const Path& src, const ModelMeta& meta, bool force);
	void createImpostorTextures(struct Model* model, ImpostorTexturesContext& ctx, bool bake_normals);
	
	const Array<ImportMesh>& getMeshes() const { return m_meshes; }
	const Array<ImportAnimation>& getAnimations() const { return m_animations; }

protected:
	ModelImporter(struct StudioApp& app);
	virtual ~ModelImporter() {}

	template <typename T> void write(const T& obj) { m_out_file.write(&obj, sizeof(obj)); }
	void write(const void* ptr, size_t size) { m_out_file.write(ptr, size); }
	void writeString(const char* str);

	// this is called when writing animations, importer must fill tracks array with keyframes
	virtual void fillTracks(const ImportAnimation& anim
		, Array<Array<Key>>& tracks
		, u32 from_frame
		, u32 num_frames
	) const = 0;

	// TODO cleanup
	void writeModelHeader();
	void writeImpostorVertices(float center_y, Vec2 bounding_cylinder);
	void writeImpostorMesh(StringView dir, StringView model_name);
	void writeMeshes(const Path& src, int mesh_idx, const ModelMeta& meta);
	void writeLODs(const ModelMeta& meta);
	void writeGeometry(const ModelMeta& meta);
	void writeGeometry(int mesh_idx);
	void writeSkeleton(const ModelMeta& meta);
	bool writePrefab(const Path& src, const ModelMeta& meta);
	bool findTexture(StringView src_dir, StringView ext, ImportTexture& tex) const;
	void bakeVertexAO(float min_ao);
	bool writeSubmodels(const Path& src, const ModelMeta& meta);
	bool writeModel(const Path& src, const ModelMeta& meta);
	bool writeAnimations(const Path& src, const ModelMeta& meta);
	bool writePhysics(const Path& src, const ModelMeta& meta);
	void centerMeshes();

	// compute AO, auto LODs, etc.
	// call this from parse when appropriate
	void postprocessCommon(const ModelMeta& meta);

	StudioApp& m_app;
	IAllocator& m_allocator;
	OutputMemoryStream m_out_file;
	struct Shader* m_impostor_shadow_shader = nullptr;

	// importers must fill these members
	Array<Bone> m_bones; // parent must be before children
	Array<ImportMaterial> m_materials;
	Array<ImportMesh> m_meshes;
	Array<ImportAnimation> m_animations;
	Array<DVec3> m_lights;
};

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

} // namespace Lumix