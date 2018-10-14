#pragma once


#include "engine/array.h"
#include "engine/blob.h"
#include "engine/fs/os_file.h"
#include "engine/geometry.h"
#include "engine/quat.h"
#include "engine/string.h"
#include "ofbx.h"


namespace Lumix
{


struct FBXImporter
{
	struct ImportConfig 
	{
		enum class Origin : int
		{
			SOURCE,
			CENTER,
			BOTTOM
		};
		const char* output_dir;
		float mesh_scale;
		Origin origin = Origin::SOURCE;
	};


	enum class Orientation
	{
		Y_UP,
		Z_UP,
		Z_MINUS_UP,
		X_MINUS_UP,
		X_UP
	};

	struct RotationKey
	{
		Quat rot;
		float time;
		u16 frame;
	};

	struct TranslationKey
	{
		Vec3 pos;
		float time;
		u16 frame;
	};

	struct Skin
	{
		float weights[4];
		i16 joints[4];
		int count = 0;
	};

	struct ImportAnimation
	{
		struct Split
		{
			int from_frame = 0;
			int to_frame = 0;
			StaticString<32> name;
		};

		explicit ImportAnimation(IAllocator& allocator)
			: splits(allocator)
		{}

		const ofbx::AnimationStack* fbx = nullptr;
		const ofbx::IScene* scene = nullptr;
		Array<Split> splits;
		StaticString<MAX_PATH_LENGTH> output_filename;
		bool import = true;
		int root_motion_bone_idx = -1;
	};

	struct ImportTexture
	{
		enum Type
		{
			DIFFUSE,
			NORMAL,
			COUNT
		};

		const ofbx::Texture* fbx = nullptr;
		bool import = true;
		bool to_dds = true;
		bool is_valid = false;
		StaticString<MAX_PATH_LENGTH> path;
		StaticString<MAX_PATH_LENGTH> src;
	};

	struct ImportMaterial
	{
		const ofbx::Material* fbx = nullptr;
		bool import = true;
		bool alpha_cutout = false;
		ImportTexture textures[ImportTexture::COUNT];
		char shader[20];
	};

	struct ImportMesh
	{
		ImportMesh(IAllocator& allocator)
			: vertex_data(allocator)
			, indices(allocator)
		{
		}

		const ofbx::Mesh* fbx = nullptr;
		const ofbx::Material* fbx_mat = nullptr;
		bool import = true;
		bool import_physics = false;
		int lod = 0;
		OutputBlob vertex_data;
		Array<int> indices;
		AABB aabb;
		float radius_squared;
	};

	FBXImporter(IAllocator& allocator);
	~FBXImporter();
	bool setSource(const char* filename);
	void writeMaterials(const char* src, const ImportConfig& cfg);
	void writeAnimations(const char* src, const ImportConfig& cfg);
	void writeSubmodels(const char* src, const ImportConfig& cfg);
	void writeModel(const char* output_mesh_filename, const char* ext, const char* src, const ImportConfig& cfg);
	void writeTextures(const char* fbx_path, const ImportConfig& cfg);

	const Array<ImportMesh>& getMeshes() const { return meshes; }
	static const char* FBXImporter::getImportMeshName(const FBXImporter::ImportMesh& mesh);

private:
	const ofbx::Mesh* getAnyMeshFromBone(const ofbx::Object* node) const;
	void gatherMaterials(const ofbx::Object* node, const char* src_dir);

	void sortBones();
	void gatherBones(const ofbx::IScene& scene);
	void gatherAnimations(const ofbx::IScene& scene);
	void writePackedVec3(const ofbx::Vec3& vec, const Matrix& mtx, OutputBlob* blob) const;
	void postprocessMeshes(const ImportConfig& cfg);
	void gatherMeshes(ofbx::IScene* scene);
	
	template <typename T> void write(const T& obj) { out_file.write(&obj, sizeof(obj)); }
	void write(const void* ptr, size_t size) { out_file.write(ptr, size); }
	void writeString(const char* str);
	bool writeBillboardMaterial(const char* output_dir, const char* src);
	bool isSkinned(const ofbx::Mesh& mesh) const { return !ignore_skeleton && mesh.getGeometry()->getSkin() != nullptr; }
	int getVertexSize(const ofbx::Mesh& mesh) const;
	void fillSkinInfo(Array<Skin>& skinning, const ofbx::Mesh* mesh) const;
	Vec3 fixRootOrientation(const Vec3& v) const;
	Quat fixRootOrientation(const Quat& v) const;
	Vec3 fixOrientation(const Vec3& v) const;
	Quat fixOrientation(const Quat& v) const;
	void writeBillboardVertices(const AABB& aabb);
	void writeGeometry();
	void writeGeometry(int mesh_idx);
	void writeBillboardMesh(i32 attribute_array_offset, i32 indices_offset, const char* mesh_output_filename);
	void writeMeshes(const char* mesh_output_filename, const char* src, int mesh_idx);
	void writeSkeleton(const ImportConfig& cfg);
	void writeLODs();
	int getAttributeCount(const ofbx::Mesh& mesh) const;
	bool areIndices16Bit(const ImportMesh& mesh) const;
	void writeModelHeader();
	void writePhysicsHeader(FS::OsFile& file) const;
	void writePhysicsTriMesh(FS::OsFile& file);
	bool writePhysics(const char* basename, const char* output_dir);

	
	IAllocator& allocator;
	Array<ImportMaterial> materials;
	Array<ImportMesh> meshes;
	Array<ImportAnimation> animations;
	Array<const ofbx::Object*> bones;
	ofbx::IScene* scene;
	float lods_distances[4] = {-10, -100, -1000, -10000};
	FS::OsFile out_file;
	float time_scale = 1.0f;
	float position_error = 0.1f;
	float rotation_error = 0.01f;
	float bounding_shape_scale = 1.0f;
	bool to_dds = false;
	bool cancel_mesh_transforms = false;
	bool ignore_skeleton = false;
	bool import_vertex_colors = true;
	bool make_convex = false;
	bool create_billboard_lod = false;
	Orientation orientation = Orientation::Y_UP;
	Orientation root_orientation = Orientation::Y_UP;
};


} // namespace Lumix