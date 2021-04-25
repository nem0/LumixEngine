#pragma once


#include "engine/array.h"
#include "engine/geometry.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/stream.h"
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
		enum class Physics {
			NONE,
			CONVEX,
			TRIMESH
		};
		float mesh_scale;
		Origin origin = Origin::SOURCE;
		bool create_impostor = false;
		bool mikktspace_tangents = false;
		bool import_vertex_colors = true;
		bool bake_vertex_ao = false;
		Physics physics = Physics::NONE;
		float lods_distances[4] = {-10, -100, -1000, -10000};
		float position_error = 0.0f;
		float rotation_error = 0.0f;
		float bounding_scale = 1.f;
	};


	enum class Orientation
	{
		Y_UP,
		Z_UP,
		Z_MINUS_UP,
		X_MINUS_UP,
		X_UP
	};

	struct Key
	{
		Vec3 pos;
		Quat rot;
		i64 time;
		u8 flags = 0;
	};

	struct Skin
	{
		float weights[4];
		i16 joints[4];
		int count = 0;
	};

	struct ImportAnimation
	{
		const ofbx::AnimationStack* fbx = nullptr;
		const ofbx::IScene* scene = nullptr;
		StaticString<LUMIX_MAX_PATH> name;
		bool import = true;
	};

	struct ImportTexture
	{
		enum Type
		{
			DIFFUSE,
			NORMAL,
			SPECULAR,
			COUNT
		};

		const ofbx::Texture* fbx = nullptr;
		bool import = true;
		bool to_dds = true;
		bool is_valid = false;
		StaticString<LUMIX_MAX_PATH> path;
		StaticString<LUMIX_MAX_PATH> src;
	};

	struct ImportMaterial
	{
		const ofbx::Material* fbx = nullptr;
		bool import = true;
		bool alpha_cutout = false;
		ImportTexture textures[ImportTexture::COUNT];
		char shader[20];
	};

	struct ImportGeometry
	{
		ImportGeometry(IAllocator& allocator)
			: indices(allocator)
			, vertex_data(allocator)
			, computed_tangents(allocator)
			, computed_normals(allocator)
		{
		}

		const ofbx::Geometry* fbx = nullptr;
		Array<u32> indices;
		OutputMemoryStream vertex_data;
		Array<ofbx::Vec3> computed_tangents;
		Array<ofbx::Vec3> computed_normals;
		u32 unique_vertex_count;
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
		bool is_skinned = false;
		int bone_idx = -1;
		bool import = true;
		u32 lod = 0;
		int submesh = -1;
		OutputMemoryStream vertex_data;
		Array<int> indices;
		AABB aabb;
		float origin_radius_squared;
		float center_radius_squared;
		Matrix transform_matrix = Matrix::IDENTITY;
		Vec3 origin = Vec3(0);
	};

	FBXImporter(struct StudioApp& app);
	~FBXImporter();
	void init();
	bool setSource(const char* filename, bool ignore_geometry, bool force_skinned);
	void writeMaterials(const char* src, const ImportConfig& cfg);
	void writeAnimations(const char* src, const ImportConfig& cfg);
	void writeSubmodels(const char* src, const ImportConfig& cfg);
	void writePrefab(const char* src, const ImportConfig& cfg);
	void writeModel(const char* src, const ImportConfig& cfg);
	void writePhysics(const char* src, const ImportConfig& cfg);
	bool createImpostorTextures(struct Model* model, Array<u32>& gb0_rgba, Array<u32>& gb1_rgba, Array<u32>& shadow, IVec2& size);

	const Array<ImportMesh>& getMeshes() const { return m_meshes; }
	const Array<ImportAnimation>& getAnimations() const { return m_animations; }

	static void getImportMeshName(const ImportMesh& mesh, char (&name)[256]);
	ofbx::IScene* getOFBXScene() { return scene; }

private:
	bool findTexture(const char* src_dir, const char* ext, FBXImporter::ImportTexture& tex) const;
	const ImportGeometry& getImportGeometry(const ofbx::Geometry* geom) const;
	const ImportMesh* getAnyMeshFromBone(const ofbx::Object* node, int bone_idx) const;
	void gatherMaterials(const char* fbx_filename, const char* src_dir);

	void sortBones(bool force_skinned);
	void gatherBones(const ofbx::IScene& scene, bool force_skinned);
	void gatherAnimations(const ofbx::IScene& scene);
	void writePackedVec3(const ofbx::Vec3& vec, const Matrix& mtx, OutputMemoryStream* blob) const;
	void postprocessMeshes(const ImportConfig& cfg, const char* path);
	void gatherMeshes(ofbx::IScene* scene);
	void gatherGeometries(ofbx::IScene* scene);
	void insertHierarchy(Array<const ofbx::Object*>& bones, const ofbx::Object* node);
	
	template <typename T> void write(const T& obj) { out_file.write(&obj, sizeof(obj)); }
	void write(const void* ptr, size_t size) { out_file.write(ptr, size); }
	void writeString(const char* str);
	int getVertexSize(const ofbx::Geometry& geom, bool is_skinned, const ImportConfig& cfg) const;
	void fillSkinInfo(Array<Skin>& skinning, const ImportMesh& mesh) const;
	Vec3 fixOrientation(const Vec3& v) const;
	Quat fixOrientation(const Quat& v) const;
	void writeImpostorVertices(const AABB& aabb);
	void writeGeometry(const ImportConfig& cfg);
	void writeGeometry(int mesh_idx, const ImportConfig& cfg);
	void writeImpostorMesh(const char* dir, const char* model_name);
	void writeMeshes(const char* src, int mesh_idx, const ImportConfig& cfg);
	void writeSkeleton(const ImportConfig& cfg);
	void writeLODs(const ImportConfig& cfg);
	int getAttributeCount(const ImportMesh& mesh, const ImportConfig& cfg) const;
	bool areIndices16Bit(const ImportMesh& mesh, const ImportConfig& cfg) const;
	void writeModelHeader();
	void writePhysicsTriMesh(OutputMemoryStream& file, const ImportConfig& cfg);

	
	IAllocator& m_allocator;
	struct FileSystem& m_filesystem;
	StudioApp& m_app;
	struct Shader* m_impostor_shadow_shader = nullptr;
	struct AssetCompiler& m_compiler;
	Array<ImportMaterial> m_materials;
	Array<ImportMesh> m_meshes;
	Array<ImportGeometry> m_geometries;
	Array<ImportAnimation> m_animations;
	Array<const ofbx::Object*> m_bones;
	Array<Matrix> m_bind_pose;
	ofbx::IScene* scene;
	OutputMemoryStream out_file;
	float m_time_scale = 1.0f;
	bool cancel_mesh_transforms = false;
	float m_fbx_scale = 1.f;
	Orientation m_orientation = Orientation::Y_UP;
};


} // namespace Lumix