#pragma once


#include "core/array.h"
#include "core/geometry.h"
#include "core/hash_map.h"
#include "core/math.h"
#include "core/path.h"
#include "core/stream.h"
#include "core/string.h"
#include "renderer/gpu/gpu.h"
#include "renderer/editor/model_importer.h"
#include "openfbx/ofbx.h"

namespace Lumix {

struct FBXImporter : ModelImporter {
	enum class Orientation {
		Y_UP,
		Z_UP,
		Z_MINUS_UP,
		X_MINUS_UP,
		X_UP
	};

	struct Skin {
		float weights[4];
		i16 joints[4];
		int count = 0;
	};

	enum class ReadFlags : u32 {
		NONE = 0,
		FORCE_SKINNED = 1 << 0,
		IGNORE_GEOMETRY = 1 << 1,
	};

	FBXImporter(StudioApp& app);
	~FBXImporter();
	void init();
	bool setSource(const Path& filename, ReadFlags flags);
	bool writeSubmodels(const Path& src, const ImportConfig& cfg);
	bool writePrefab(const Path& src, const ImportConfig& cfg, bool split_meshes);

	ofbx::IScene* getOFBXScene() { return m_scene; }

private:
	void getImportMeshName(ImportMesh& mesh, const ofbx::Mesh* fbx_mesh) const;
	const ImportMesh* getAnyMeshFromBone(const ofbx::Object* node, int bone_idx) const;

	void sortBones(bool force_skinned);
	void gatherBones(bool force_skinned);
	void gatherAnimations(StringView src);
	void postprocess(const ImportConfig& cfg, const Path& path) override;
	void gatherMeshes(StringView fbx_filename, StringView src_dir);
	void insertHierarchy(const ofbx::Object* node);
	
	void fillSkinInfo(Array<Skin>& skinning, const ImportMesh& mesh) const;
	Vec3 fixOrientation(const Vec3& v) const;
	Quat fixOrientation(const Quat& v) const;
	Matrix fixOrientation(const Matrix& v) const;
	void remap(const OutputMemoryStream& unindexed_triangles, ImportMesh& mesh, u32 vertex_size, const ImportConfig& cfg) const;
	void triangulate(OutputMemoryStream& unindexed_triangles
		, ImportMesh& mesh
		, const ofbx::GeometryPartition& partition
		, u32 vertex_size
		, const Array<Skin>& skinning
		, const ImportConfig& cfg
		, const Matrix& matrix
		, Array<i32>& tri_indices);
	void fillTracks(const ImportAnimation& anim
		, Array<Array<Key>>& tracks
		, u32 from_sample
		, u32 num_samples) const override;

	Array<const ofbx::Mesh*> m_fbx_meshes;
	ofbx::IScene* m_scene;
	Orientation m_orientation = Orientation::Y_UP;
};


} // namespace Lumix