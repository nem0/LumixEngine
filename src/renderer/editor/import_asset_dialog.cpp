#include "import_asset_dialog.h"
#include "animation/animation.h"
#include "editor/metadata.h"
#include "editor/platform_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/debug/floating_points.h"
#include "engine/engine.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/os_file.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math_utils.h"
#include "engine/mt/task.h"
#include "engine/mt/thread.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/reflection.h"
#include "engine/system.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "ofbx.h"
#include "physics/physics_geometry_manager.h"
#include "renderer/frame_buffer.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#if defined _MSC_VER && _MSC_VER == 1900 
#pragma warning(disable : 4312)
#endif
#include "stb/stb_image.h"
#include "stb/stb_image_resize.h"
#include <cstddef>
#include <crnlib.h>


namespace Lumix
{


typedef StaticString<MAX_PATH_LENGTH> PathBuilder;


static u32 packF4u(const Vec3& vec)
{
	const u8 xx = u8(vec.x * 127.0f + 128.0f);
	const u8 yy = u8(vec.y * 127.0f + 128.0f);
	const u8 zz = u8(vec.z * 127.0f + 128.0f);
	const u8 ww = u8(0);

	union {
		u32 ui32;
		u8 arr[4];
	} un;

	un.arr[0] = xx;
	un.arr[1] = yy;
	un.arr[2] = zz;
	un.arr[3] = ww;

	return un.ui32;
}


template <typename T, typename T2>
struct GenericTask LUMIX_FINAL : public MT::Task
{
	GenericTask(T _function, T2 _on_destroy, IAllocator& allocator)
		: Task(allocator)
		, function(_function)
		, on_destroy(_on_destroy)
	{
	}


	~GenericTask()
	{
		on_destroy();
	}


	int task() override
	{
		function();
		return 0;
	}


	T2 on_destroy;
	T function;
};


template <typename T, typename T2> GenericTask<T, T2>* makeTask(T function, T2 on_destroy, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, GenericTask<T, T2>)(function, on_destroy, allocator);
}


static crn_comp_params s_default_comp_params;


struct BillboardSceneData
{
	#pragma pack(1)
		struct Vertex
		{
			Vec3 pos;
			u8 normal[4];
			u8 tangent[4];
			Vec2 uv;
		};
	#pragma pack()

	static const int TEXTURE_SIZE = 512;

	int width;
	int height;
	float ortho_size;
	Vec3 position;


	static int ceilPowOf2(int value)
	{
		ASSERT(value > 0);
		int ret = value - 1;
		ret |= ret >> 1;
		ret |= ret >> 2;
		ret |= ret >> 3;
		ret |= ret >> 8;
		ret |= ret >> 16;
		return ret + 1;
	}


	BillboardSceneData(const AABB& aabb, int texture_size)
	{
		Vec3 size = aabb.max - aabb.min;
		float right = aabb.max.x + size.z + size.x + size.z;
		float left = aabb.min.x;
		position.set((right + left) * 0.5f, (aabb.max.y + aabb.min.y) * 0.5f, aabb.max.z + 5);

		if (2 * size.x + 2 * size.z > size.y)
		{
			width = texture_size;
			int nonceiled_height = int(width / (2 * size.x + 2 * size.z) * size.y);
			height = ceilPowOf2(nonceiled_height);
			ortho_size = size.y * height / nonceiled_height * 0.5f;
		}
		else
		{
			height = texture_size;
			width = ceilPowOf2(int(height * (2 * size.x + 2 * size.z) / size.y));
			ortho_size = size.y * 0.5f;
		}
	}


	Matrix computeMVPMatrix()
	{
		Matrix mvp = Matrix::IDENTITY;

		float ratio = height > 0 ? (float)width / height : 1.0f;
		Matrix proj;
		proj.setOrtho(-ortho_size * ratio,
			ortho_size * ratio,
			-ortho_size,
			ortho_size,
			0.0001f,
			10000.0f,
			false /* we do not care for z value, so both true and false are correct*/);

		mvp.setTranslation(position);
		mvp.fastInverse();
		mvp = proj * mvp;

		return mvp;
	}
};


struct FBXImporter
{
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

		ImportAnimation(IAllocator& allocator)
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


	const ofbx::Mesh* getAnyMeshFromBone(const ofbx::Object* node) const
	{
		for (int i = 0; i < meshes.size(); ++i)
		{
			const ofbx::Mesh* mesh = meshes[i].fbx;

			auto* skin = mesh->getGeometry()->getSkin();
			if (!skin) continue;

			for (int j = 0, c = skin->getClusterCount(); j < c; ++j)
			{
				if (skin->getCluster(j)->getLink() == node) return mesh;
			}
		}
		return nullptr;
	}


	void splitMeshes()
	{
		for (ImportMesh& mesh : meshes)
		{

		}
	}


	static ofbx::Matrix makeOFBXIdentity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }


	static ofbx::Matrix getBindPoseMatrix(const ofbx::Mesh* mesh, const ofbx::Object* node)
	{
		if (!mesh) return makeOFBXIdentity();

		auto* skin = mesh->getGeometry()->getSkin();

		for (int i = 0, c = skin->getClusterCount(); i < c; ++i)
		{
			const ofbx::Cluster* cluster = skin->getCluster(i);
			if (cluster->getLink() == node)
			{
				return cluster->getTransformLinkMatrix();
			}
		}
		ASSERT(false);
		return makeOFBXIdentity();
	}


	void gatherMaterials(const ofbx::Object* node, const char* src_dir)
	{
		for (ImportMesh& mesh : meshes)
		{
			const ofbx::Material* fbx_mat = mesh.fbx_mat;
			if (!fbx_mat) continue;

			ImportMaterial& mat = materials.emplace();
			mat.fbx = fbx_mat;

			auto gatherTexture = [&mat, src_dir](ofbx::Texture::TextureType type) {
				const ofbx::Texture* texture = mat.fbx->getTexture(type);
				if (!texture) return;

				ImportTexture& tex = mat.textures[type];
				tex.fbx = texture;
				ofbx::DataView filename = tex.fbx->getRelativeFileName();
				if (filename == "") filename = tex.fbx->getFileName();
				filename.toString(tex.path.data);
				tex.src = tex.path;
				tex.is_valid = PlatformInterface::fileExists(tex.src);

				if (!tex.is_valid)
				{
					PathUtils::FileInfo file_info(tex.path);
					tex.src = src_dir;
					tex.src << file_info.m_basename << "." << file_info.m_extension;
					tex.is_valid = PlatformInterface::fileExists(tex.src);

					if (!tex.is_valid)
					{
						tex.src = src_dir;
						tex.src << tex.path;
						tex.is_valid = PlatformInterface::fileExists(tex.src);
					}
				}

				tex.import = true;
				tex.to_dds = true;
			};

			gatherTexture(ofbx::Texture::DIFFUSE);
			gatherTexture(ofbx::Texture::NORMAL);
		}
	}


	static void insertHierarchy(Array<const ofbx::Object*>& bones, const ofbx::Object* node)
	{
		if (!node) return;
		if (bones.indexOf(node) >= 0) return;
		ofbx::Object* parent = node->getParent();
		insertHierarchy(bones, parent);
		bones.push(node);
	}


	void sortBones()
	{
		int count = bones.size();
		for (int i = 0; i < count; ++i)
		{
			for (int j = i + 1; j < count; ++j)
			{
				if (bones[i]->getParent() == bones[j])
				{
					const ofbx::Object* bone = bones[j];
					bones.eraseFast(j);
					bones.insert(i, bone);
					--i;
					break;
				}
			}
		}
	}


	void gatherBones(const ofbx::IScene& scene)
	{
		for (const ImportMesh& mesh : meshes)
		{
			const ofbx::Skin* skin = mesh.fbx->getGeometry()->getSkin();
			if (skin)
			{
				for (int i = 0; i < skin->getClusterCount(); ++i)
				{
					const ofbx::Cluster* cluster = skin->getCluster(i);
					insertHierarchy(bones, cluster->getLink());
				}
			}
		}

		bones.removeDuplicates();
		sortBones();
	}


	static void makeValidFilename(char* filename)
	{
		char* c = filename;
		while (*c)
		{
			bool is_valid = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
			if (!is_valid) *c = '_';
			++c;
		}
	}


	void gatherAnimations(const ofbx::IScene& scene)
	{
		int anim_count = scene.getAnimationStackCount();
		for (int i = 0; i < anim_count; ++i)
		{
			IAllocator& allocator = app.getWorldEditor().getAllocator();
			ImportAnimation& anim = animations.emplace(allocator);
			anim.scene = &scene;
			anim.fbx = (const ofbx::AnimationStack*)scene.getAnimationStack(i);
			anim.import = true;
			const ofbx::TakeInfo* take_info = scene.getTakeInfo(anim.fbx->name);
			if (take_info)
			{
				if (take_info->name.begin != take_info->name.end)
				{
					take_info->name.toString(anim.output_filename.data);
				}
				if (anim.output_filename.empty() && take_info->filename.begin != take_info->filename.end)
				{
					take_info->filename.toString(anim.output_filename.data);
				}
				if (anim.output_filename.empty()) anim.output_filename << "anim";
			}
			else
			{
				anim.output_filename = "anim";
			}

			makeValidFilename(anim.output_filename.data);
		}
	}


	static int findSubblobIndex(const OutputBlob& haystack, const OutputBlob& needle, const Array<int>& subblobs, int first_subblob)
	{
		const u8* data = (const u8*)haystack.getData();
		const u8* needle_data = (const u8*)needle.getData();
		int step_size = needle.getPos();
		int step_count = haystack.getPos() / step_size;
		int idx = first_subblob;
		while(idx != -1)
		{
			if (compareMemory(data + idx * step_size, needle_data, step_size) == 0) return idx;
			idx = subblobs[idx];
		}
		return -1;
	}


	void writePackedVec3(const ofbx::Vec3& vec, const Matrix& mtx, OutputBlob* blob) const
	{
		Vec3 v = toLumixVec3(vec);
		v = mtx * Vec4(v, 0);
		v.normalize();
		v = fixOrientation(v);

		u32 packed = packF4u(v);
		blob->write(packed);
	}


	static void writeUV(const ofbx::Vec2& uv, OutputBlob* blob)
	{
		Vec2 tex_cooords = {(float)uv.x, 1 - (float)uv.y};
		blob->write(tex_cooords);
	}


	static void writeColor(const ofbx::Vec4& color, OutputBlob* blob)
	{
		u8 rgba[4];
		rgba[0] = u8(color.x * 255);
		rgba[1] = u8(color.y * 255);
		rgba[2] = u8(color.z * 255);
		rgba[3] = u8(color.w * 255);
		blob->write(rgba);
	}


	static void writeSkin(const Skin& skin, OutputBlob* blob)
	{
		blob->write(skin.joints);
		blob->write(skin.weights);
		float sum = skin.weights[0] + skin.weights[1] + skin.weights[2] + skin.weights[3];
		ASSERT(sum > 0.99f && sum < 1.01f);
	}


	static int getMaterialIndex(const ofbx::Mesh& mesh, const ofbx::Material& material)
	{
		for (int i = 0, c = mesh.getMaterialCount(); i < c; ++i)
		{
			if (mesh.getMaterial(i) == &material) return i;
		}
		return -1;
	}


	void postprocessMeshes()
	{
		for (int mesh_idx = 0; mesh_idx < meshes.size(); ++mesh_idx)
		{
			ImportMesh& import_mesh = meshes[mesh_idx];
			dialog.setImportMessage("Processing meshes...", (mesh_idx / (float)meshes.size()) * 0.4f);
			import_mesh.vertex_data.clear();
			import_mesh.indices.clear();

			const ofbx::Mesh& mesh = *import_mesh.fbx;
			const ofbx::Geometry* geom = import_mesh.fbx->getGeometry();
			int vertex_count = geom->getVertexCount();
			const ofbx::Vec3* vertices = geom->getVertices();
			const ofbx::Vec3* normals = geom->getNormals();
			const ofbx::Vec3* tangents = geom->getTangents();
			const ofbx::Vec4* colors = import_vertex_colors ? geom->getColors() : nullptr;
			const ofbx::Vec2* uvs = geom->getUVs();

			Matrix transform_matrix = Matrix::IDENTITY;
			Matrix geometry_matrix = toLumix(mesh.getGeometricMatrix());
			transform_matrix = toLumix(mesh.getGlobalTransform()) * geometry_matrix;
			if (center_mesh) transform_matrix.setTranslation({0, 0, 0});

			IAllocator& allocator = app.getWorldEditor().getAllocator();
			OutputBlob blob(allocator);
			int vertex_size = getVertexSize(mesh);
			import_mesh.vertex_data.reserve(vertex_count * vertex_size);

			Array<Skin> skinning(allocator);
			bool is_skinned = isSkinned(mesh);
			if (is_skinned) fillSkinInfo(skinning, &mesh);

			AABB aabb = {{0, 0, 0}, {0, 0, 0}};
			float radius_squared = 0;

			int material_idx = getMaterialIndex(mesh, *import_mesh.fbx_mat);
			assert(material_idx >= 0);

			int first_subblob[256];
			for (int& subblob : first_subblob) subblob = -1;
			Array<int> subblobs(allocator);
			subblobs.reserve(vertex_count);

			int default_mat = 0;
			const int* materials = geom->getMaterials();
			for (int i = 0; i < vertex_count; ++i)
			{
				if (materials && materials[i / 3] != material_idx) continue;

				blob.clear();
				ofbx::Vec3 cp = vertices[i];
				// premultiply control points here, so we can have constantly-scaled meshes without scale in bones
				Vec3 pos = transform_matrix.transformPoint(toLumixVec3(cp)) * mesh_scale;
				pos = fixOrientation(pos);
				blob.write(pos);

				float sq_len = pos.squaredLength();
				radius_squared = Math::maximum(radius_squared, sq_len);

				aabb.min.x = Math::minimum(aabb.min.x, pos.x);
				aabb.min.y = Math::minimum(aabb.min.y, pos.y);
				aabb.min.z = Math::minimum(aabb.min.z, pos.z);
				aabb.max.x = Math::maximum(aabb.max.x, pos.x);
				aabb.max.y = Math::maximum(aabb.max.y, pos.y);
				aabb.max.z = Math::maximum(aabb.max.z, pos.z);

				if (normals) writePackedVec3(normals[i], transform_matrix, &blob);
				if (uvs) writeUV(uvs[i], &blob);
				if (colors) writeColor(colors[i], &blob);
				if (tangents) writePackedVec3(tangents[i], transform_matrix, &blob);
				if (is_skinned) writeSkin(skinning[i], &blob);

				u8 first_byte = ((const u8*)blob.getData())[0];

				int idx = findSubblobIndex(import_mesh.vertex_data, blob, subblobs, first_subblob[first_byte]);
				if (idx == -1)
				{
					subblobs.push(first_subblob[first_byte]);
					first_subblob[first_byte] = subblobs.size() - 1;
					import_mesh.indices.push(import_mesh.vertex_data.getPos() / vertex_size);
					import_mesh.vertex_data.write(blob.getData(), vertex_size);
				}
				else
				{
					import_mesh.indices.push(idx);
				}
			}

			import_mesh.aabb = aabb;
			import_mesh.radius_squared = radius_squared;
		}
		for (int mesh_idx = meshes.size() - 1; mesh_idx >= 0; --mesh_idx)
		{
			if (meshes[mesh_idx].indices.empty()) meshes.eraseFast(mesh_idx);
		}
	}


	void gatherMeshes(ofbx::IScene* scene)
	{
		IAllocator& allocator = app.getWorldEditor().getAllocator();
		int c = scene->getMeshCount();
		for (int i = 0; i < c; ++i)
		{
			const ofbx::Mesh* fbx_mesh = (const ofbx::Mesh*)scene->getMesh(i);
			if (fbx_mesh->getGeometry()->getVertexCount() == 0) continue;
			for (int j = 0; j < fbx_mesh->getMaterialCount(); ++j)
			{
				ImportMesh& mesh = meshes.emplace(allocator);
				mesh.fbx = fbx_mesh;
				mesh.fbx_mat = fbx_mesh->getMaterial(j);
				mesh.lod = detectMeshLOD(mesh);
			}
		}
	}


	static int detectMeshLOD(const ImportMesh& mesh)
	{
		const char* node_name = mesh.fbx->name;
		const char* lod_str = stristr(node_name, "_LOD");
		if (!lod_str)
		{
			const char* mesh_name = getImportMeshName(mesh);
			if (!mesh_name) return 0;

			const char* lod_str = stristr(mesh_name, "_LOD");
			if (!lod_str) return 0;
		}

		lod_str += stringLength("_LOD");

		int lod;
		fromCString(lod_str, stringLength(lod_str), &lod);

		return lod;
	}


	static Vec3 toLumixVec3(const ofbx::Vec4& v) { return {(float)v.x, (float)v.y, (float)v.z}; }
	static Vec3 toLumixVec3(const ofbx::Vec3& v) { return {(float)v.x, (float)v.y, (float)v.z}; }
	static Quat toLumix(const ofbx::Quat& q) { return {(float)q.x, (float)q.y, (float)q.z, (float)q.w}; }


	static Matrix toLumix(const ofbx::Matrix& mtx)
	{
		Matrix res;

		for (int i = 0; i < 16; ++i) (&res.m11)[i] = (float)mtx.m[i];

		return res;
	}


	FBXImporter(StudioApp& _app, ImportAssetDialog& _dialog)
		: app(_app)
		, dialog(_dialog)
		, scenes(_app.getWorldEditor().getAllocator())
		, materials(_app.getWorldEditor().getAllocator())
		, meshes(_app.getWorldEditor().getAllocator())
		, animations(_app.getWorldEditor().getAllocator())
		, bones(_app.getWorldEditor().getAllocator())
	{
	}


	bool addSource(const char* filename)
	{
		IAllocator& allocator = app.getWorldEditor().getAllocator();

		FS::OsFile file;
		if (!file.open(filename, FS::Mode::OPEN_AND_READ)) return false;

		Array<u8> data(allocator);
		data.resize((int)file.size());

		if (!file.read(&data[0], data.size()))
		{
			file.close();
			return false;
		}
		file.close();

		ofbx::IScene* scene = ofbx::load(&data[0], data.size());
		if (!scene)
		{
			g_log_error.log("FBX") << "Failed to import \"" << filename << ": " << ofbx::getError();
			return false;
		}

		const ofbx::Object* root = scene->getRoot();
		char src_dir[MAX_PATH_LENGTH];
		PathUtils::getDir(src_dir, lengthOf(src_dir), filename);
		gatherMeshes(scene);
		gatherMaterials(root, src_dir);
		materials.removeDuplicates([](const ImportMaterial& a, const ImportMaterial& b) { return a.fbx == b.fbx; });
		gatherBones(*scene);
		gatherAnimations(*scene);

		scenes.push(scene);
		return true;
	}


	template <typename T> void write(const T& obj) { out_file.write(&obj, sizeof(obj)); }
	void write(const void* ptr, size_t size) { out_file.write(ptr, size); }
	void writeString(const char* str) { out_file.write(str, strlen(str)); }


	static void getRelativePath(WorldEditor& editor, char* relative_path, int max_length, const char* source)
	{
		char tmp[MAX_PATH_LENGTH];
		PathUtils::normalize(source, tmp, sizeof(tmp));

		const char* base_path = editor.getEngine().getDiskFileDevice()->getBasePath();
		if (compareStringN(base_path, tmp, stringLength(base_path)) == 0)
		{
			int base_path_length = stringLength(base_path);
			const char* rel_path_start = tmp + base_path_length;
			if (rel_path_start[0] == '/')
			{
				++rel_path_start;
			}
			copyString(relative_path, max_length, rel_path_start);
		}
		else
		{
			auto* patch_fd = editor.getEngine().getPatchFileDevice();
			const char* base_path = patch_fd ? patch_fd->getBasePath() : nullptr;
			if (base_path && compareStringN(base_path, tmp, stringLength(base_path)) == 0)
			{
				int base_path_length = stringLength(base_path);
				const char* rel_path_start = tmp + base_path_length;
				if (rel_path_start[0] == '/')
				{
					++rel_path_start;
				}
				copyString(relative_path, max_length, rel_path_start);
			}
			else
			{
				copyString(relative_path, max_length, tmp);
			}
		}
	}


	bool writeBillboardMaterial(const char* output_dir, const char* texture_output_dir, const char* mesh_output_filename)
	{
		if (!create_billboard_lod) return true;

		FS::OsFile file;
		PathBuilder output_material_name(output_dir, "/", mesh_output_filename, "_billboard.mat");
		IAllocator& allocator = app.getWorldEditor().getAllocator();
		if (!file.open(output_material_name, FS::Mode::CREATE_AND_WRITE))
		{
			g_log_error.log("FBX") << "Failed to create " << output_material_name;
			return false;
		}
		file << "{\n\t\"shader\" : \"pipelines/rigid/rigid.shd\"\n";
		file << "\t, \"defines\" : [\"ALPHA_CUTOUT\"]\n";
		file << "\t, \"texture\" : {\n\t\t\"source\" : \"";

		WorldEditor& editor = app.getWorldEditor();
		if (texture_output_dir[0])
		{
			char from_root_path[MAX_PATH_LENGTH];
			getRelativePath(editor, from_root_path, lengthOf(from_root_path), texture_output_dir);
			PathBuilder relative_texture_path(from_root_path, mesh_output_filename, "_billboard.dds");
			PathBuilder texture_path(texture_output_dir, mesh_output_filename, "_billboard.dds");
			copyFile("models/utils/cube/default.dds", texture_path);
			file << "/" << relative_texture_path << "\"}\n\t, \"texture\" : {\n\t\t\"source\" : \"";

			PathBuilder relative_normal_path_n(from_root_path, mesh_output_filename, "_billboard_normal.dds");
			PathBuilder normal_path(texture_output_dir, mesh_output_filename, "_billboard_normal.dds");
			copyFile("models/utils/cube/default.dds", normal_path);
			file << "/" << relative_normal_path_n;

		}
		else
		{
			file << mesh_output_filename << "_billboard.dds\"}\n\t, \"texture\" : {\n\t\t\"source\" : \"";
			PathBuilder texture_path(output_dir, "/", mesh_output_filename, "_billboard.dds");
			copyFile("models/utils/cube/default.dds", texture_path);

			file << mesh_output_filename << "_billboard_normal.dds";
			PathBuilder normal_path(output_dir, "/", mesh_output_filename, "_billboard_normal.dds");
			copyFile("models/utils/cube/default.dds", normal_path);
		}

		file << "\"}\n}";
		file.close();
		return true;
	}


	void writeMaterials(const char* output_dir, const char* texture_output_dir, const char* mesh_output_filename)
	{
		dialog.setImportMessage("Writing materials...", 0.9f);

		for (const ImportMaterial& material : materials)
		{
			if (!material.import) continue;

			char mat_name[128];
			getMaterialName(material.fbx, mat_name);
			StaticString<MAX_PATH_LENGTH> path(output_dir, mat_name, ".mat");
			IAllocator& allocator = app.getWorldEditor().getAllocator();
			if (!out_file.open(path, FS::Mode::CREATE_AND_WRITE))
			{
				g_log_error.log("FBX") << "Failed to create " << path;
				continue;
			}

			writeString("{\n\t\"shader\" : \"pipelines/rigid/rigid.shd\"");
			if (material.alpha_cutout) writeString(",\n\t\"defines\" : [\"ALPHA_CUTOUT\"]");
			auto writeTexture = [this, texture_output_dir](const ImportTexture& texture, bool srgb) {
				if (texture.fbx)
				{
					writeString(",\n\t\"texture\" : { \"source\" : \"");
					PathUtils::FileInfo info(texture.src);
					writeString(texture_output_dir);
					writeString(info.m_basename);
					writeString(".");
					writeString(texture.to_dds ? "dds" : info.m_extension);
					writeString("\"");
					if (srgb) writeString(", \"srgb\" : true ");
					writeString("}");
				}
				else
				{
					writeString(",\n\t\"texture\" : {");
					if (srgb) writeString(" \"srgb\" : true ");
					writeString("}");
				}


			};

			writeTexture(material.textures[0], true);
			writeTexture(material.textures[1], false);

			writeString("}");

			out_file.close();
		}
		writeBillboardMaterial(output_dir, texture_output_dir,  mesh_output_filename);
	}


	static Vec3 getTranslation(const ofbx::Matrix& mtx)
	{
		return {(float)mtx.m[12], (float)mtx.m[13], (float)mtx.m[14]};
	}


	static Quat getRotation(const ofbx::Matrix& mtx)
	{
		Matrix m = toLumix(mtx);
		m.normalizeScale();
		return m.getRotation();
	}


	// arg parent_scale - animated scale is not supported, but we can get rid of static scale if we ignore
	// it in writeSkeleton() and use parent_scale in this function
	static void compressPositions(Array<TranslationKey>& out,
		int from_frame,
		int to_frame,
		float sample_period,
		const ofbx::AnimationCurveNode* curve_node,
		const ofbx::Object& bone,
		float error,
		float parent_scale)
	{
		out.clear();
		if (!curve_node) return;
		if (to_frame == from_frame) return;

		ofbx::Vec3 lcl_rotation = bone.getLocalRotation();
		Vec3 pos = getTranslation(bone.evalLocal(curve_node->getNodeLocalTransform(from_frame * sample_period), lcl_rotation)) * parent_scale;
		TranslationKey last_written = {pos, 0, 0};
		out.push(last_written);
		if (to_frame == from_frame + 1) return;

		pos = getTranslation(bone.evalLocal(curve_node->getNodeLocalTransform((from_frame + 1) * sample_period), lcl_rotation)) *
			  parent_scale;
		Vec3 dif = (pos - last_written.pos) / sample_period;
		TranslationKey prev = {pos, sample_period, 1};
		float dt = sample_period;
		for (u16 i = 2; i < u16(to_frame - from_frame); ++i)
		{
			float t = i * sample_period;
			Vec3 cur =
				getTranslation(bone.evalLocal(curve_node->getNodeLocalTransform((from_frame + i) * sample_period), lcl_rotation)) * parent_scale;
			dt = t - last_written.time;
			Vec3 estimate = last_written.pos + dif * dt;
			if (fabs(estimate.x - cur.x) > error || fabs(estimate.y - cur.y) > error ||
				fabs(estimate.z - cur.z) > error)
			{
				last_written = prev;
				out.push(last_written);

				dt = sample_period;
				dif = (cur - last_written.pos) / dt;
			}
			prev = {cur, t, i};
		}

		float t = (to_frame - from_frame) * sample_period;
		last_written = {
			getTranslation(bone.evalLocal(curve_node->getNodeLocalTransform(to_frame * sample_period), lcl_rotation)) * parent_scale,
			t,
			u16(to_frame - from_frame)};
		out.push(last_written);
	}


	static void compressRotations(Array<RotationKey>& out,
		int from_frame,
		int to_frame,
		float sample_period,
		const ofbx::AnimationCurveNode* curve_node,
		const ofbx::Object& bone,
		float error)
	{
		out.clear();
		if (!curve_node) return;
		if (to_frame == from_frame) return;

		float dt = sample_period;
		ofbx::Vec3 lcl_translation = bone.getLocalTranslation();
		Quat rot = getRotation(bone.evalLocal(lcl_translation, curve_node->getNodeLocalTransform(from_frame * sample_period)));
		RotationKey last_written = {rot, 0, 0};
		out.push(last_written);
		if (to_frame == from_frame + 1) return;

		rot = getRotation(bone.evalLocal(lcl_translation, curve_node->getNodeLocalTransform((from_frame + 1) * sample_period)));
		RotationKey after_last = {rot, sample_period, 1};
		RotationKey prev = after_last;
		for (u16 i = 2; i < u16(to_frame - from_frame); ++i)
		{
			float t = i * sample_period;
			Quat cur = getRotation(bone.evalLocal(lcl_translation, curve_node->getNodeLocalTransform((from_frame + i) * sample_period)));
			Quat estimate;
			nlerp(cur, last_written.rot, &estimate, sample_period / (t - last_written.time));
			if (fabs(estimate.x - after_last.rot.x) > error || fabs(estimate.y - after_last.rot.y) > error ||
				fabs(estimate.z - after_last.rot.z) > error)
			{
				last_written = prev;
				out.push(last_written);

				after_last = {cur, t, i};
			}
			prev = {cur, t, i};
		}

		float t = (to_frame - from_frame) * sample_period;
		last_written = {
			getRotation(bone.evalLocal(lcl_translation, curve_node->getNodeLocalTransform(to_frame * sample_period))), 
			t, 
			u16(to_frame - from_frame)};
		out.push(last_written);
	}


	static float getScaleX(const ofbx::Matrix& mtx)
	{
		Vec3 v(float(mtx.m[0]), float(mtx.m[4]), float(mtx.m[8]));

		return v.length();
	}


	static int getDepth(const ofbx::Object* bone)
	{
		int depth = 0;
		while (bone)
		{
			++depth;
			bone = bone->getParent();
		}
		return depth;
	}


	void writeAnimations(const char* output_dir)
	{
		for (int anim_idx = 0; anim_idx < animations.size(); ++anim_idx)
		{
			ImportAnimation& anim = animations[anim_idx];
			dialog.setImportMessage("Writing animation...", 0.6f + 0.2f * (anim_idx / (float)animations.size()));
			if (!anim.import) continue;
			const ofbx::AnimationStack* stack = anim.fbx;
			const char* anim_name = stack->name;
			const ofbx::IScene& scene = *anim.scene;
			const ofbx::TakeInfo* take_info = scene.getTakeInfo(stack->name);

			float fbx_frame_rate = scene.getSceneFrameRate();
			if (fbx_frame_rate < 0) fbx_frame_rate = 24;
			float scene_frame_rate = fbx_frame_rate / time_scale;
			float sampling_period = 1.0f / scene_frame_rate;
			int all_frames_count = 0;
			if (take_info)
			{
				all_frames_count = int((take_info->local_time_to - take_info->local_time_from) / sampling_period + 0.5f);
			}
			else
			{
				ASSERT(false);
				// TODO
				// scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(time_spawn);
			}

			// TODO
			/*FbxTime::EMode mode = scene->GetGlobalSettings().GetTimeMode();
			float scene_frame_rate =
			(float)((mode == FbxTime::eCustom) ? scene->GetGlobalSettings().GetCustomFrameRate()
			: FbxTime::GetFrameRate(mode));
			*/
			for (int i = 0; i < Math::maximum(1, anim.splits.size()); ++i)
			{
				FBXImporter::ImportAnimation::Split whole_anim_split;
				whole_anim_split.to_frame = all_frames_count;
				auto* split = anim.splits.empty() ? &whole_anim_split : &anim.splits[i];

				float begin = sampling_period * split->from_frame;
				float end = sampling_period * split->to_frame;

				float duration = (end > begin ? end - begin : 1.0f) * time_scale;
				int frame_count = split->to_frame - split->from_frame;

				StaticString<MAX_PATH_LENGTH> tmp(output_dir, anim.output_filename, split->name, ".ani");
				IAllocator& allocator = app.getWorldEditor().getAllocator();
				if (!out_file.open(tmp, FS::Mode::CREATE_AND_WRITE))
				{
					g_log_error.log("FBX") << "Failed to create " << tmp;
					continue;
				}
				Animation::Header header;
				header.magic = Animation::HEADER_MAGIC;
				header.version = 3;
				header.fps = (u32)(scene_frame_rate + 0.5f);
				write(header);

				write(anim.root_motion_bone_idx);
				write(frame_count);
				int used_bone_count = 0;

				for (const ofbx::Object* bone : bones)
				{
					if (&bone->getScene() != &scene) continue;

					const ofbx::AnimationLayer* layer = stack->getLayer(0);
					const ofbx::AnimationCurveNode* translation_curve_node = layer->getCurveNode(*bone, "Lcl Translation");
					const ofbx::AnimationCurveNode* rotation_curve_node = layer->getCurveNode(*bone, "Lcl Rotation");
					if (translation_curve_node || rotation_curve_node) ++used_bone_count;
				}

				write(used_bone_count);
				Array<TranslationKey> positions(allocator);
				Array<RotationKey> rotations(allocator);
				for (const ofbx::Object* bone : bones)
				{
					if (&bone->getScene() != &scene) continue;
					const ofbx::Object* root_bone = anim.root_motion_bone_idx >= 0 ? bones[anim.root_motion_bone_idx] : nullptr;

					const ofbx::AnimationLayer* layer = stack->getLayer(0);
					const ofbx::AnimationCurveNode* translation_node = layer->getCurveNode(*bone, "Lcl Translation");
					const ofbx::AnimationCurveNode* rotation_node = layer->getCurveNode(*bone, "Lcl Rotation");
					if (!translation_node && !rotation_node) continue;

					u32 name_hash = crc32(bone->name);
					write(name_hash);

					int depth = getDepth(bone);
					float parent_scale = bone->getParent() ? (float)getScaleX(bone->getParent()->getGlobalTransform()) : 1;
					compressPositions(positions, split->from_frame, split->to_frame, sampling_period, translation_node, *bone, position_error / depth, parent_scale);
					write(positions.size());

					for (TranslationKey& key : positions) write(key.frame);
					for (TranslationKey& key : positions)
					{
						if (bone == root_bone)
						{
							write(fixRootOrientation(key.pos * mesh_scale));
						}
						else
						{
							write(fixOrientation(key.pos * mesh_scale));
						}
					}

					compressRotations(rotations, split->from_frame, split->to_frame, sampling_period, rotation_node, *bone, rotation_error / depth);

					write(rotations.size());
					for (RotationKey& key : rotations) write(key.frame);
					for (RotationKey& key : rotations)
					{
						if (bone == root_bone)
						{
							write(fixRootOrientation(key.rot));
						}
						else
						{
							write(fixOrientation(key.rot));
						}
					}
				}
				out_file.close();
			}
		}
	}


	bool isSkinned(const ofbx::Mesh& mesh) const { return !ignore_skeleton && mesh.getGeometry()->getSkin() != nullptr; }


	int getVertexSize(const ofbx::Mesh& mesh) const
	{
		static const int POSITION_SIZE = sizeof(float) * 3;
		static const int NORMAL_SIZE = sizeof(u8) * 4;
		static const int TANGENT_SIZE = sizeof(u8) * 4;
		static const int UV_SIZE = sizeof(float) * 2;
		static const int COLOR_SIZE = sizeof(u8) * 4;
		static const int BONE_INDICES_WEIGHTS_SIZE = sizeof(float) * 4 + sizeof(u16) * 4;
		int size = POSITION_SIZE;

		if (mesh.getGeometry()->getNormals()) size += NORMAL_SIZE;
		if (mesh.getGeometry()->getUVs()) size += UV_SIZE;
		if (mesh.getGeometry()->getColors() && import_vertex_colors) size += COLOR_SIZE;
		if (mesh.getGeometry()->getTangents()) size += TANGENT_SIZE;
		if (isSkinned(mesh)) size += BONE_INDICES_WEIGHTS_SIZE;

		return size;
	}


	void fillSkinInfo(Array<Skin>& skinning, const ofbx::Mesh* mesh) const
	{
		const ofbx::Geometry* geom = mesh->getGeometry();
		skinning.resize(geom->getVertexCount());

		auto* skin = mesh->getGeometry()->getSkin();
		for (int i = 0, c = skin->getClusterCount(); i < c; ++i)
		{
			const ofbx::Cluster* cluster = skin->getCluster(i);
			if (cluster->getIndicesCount() == 0) continue;
			int joint = bones.indexOf(cluster->getLink());
			ASSERT(joint >= 0);
			const int* cp_indices = cluster->getIndices();
			const double* weights = cluster->getWeights();
			for (int j = 0; j < cluster->getIndicesCount(); ++j)
			{
				int idx = cp_indices[j];
				float weight = (float)weights[j];
				Skin& s = skinning[idx];
				if (s.count < 4)
				{
					s.weights[s.count] = weight;
					s.joints[s.count] = joint;
					++s.count;
				}
				else
				{
					int min = 0;
					for (int m = 1; m < 4; ++m)
					{
						if (s.weights[m] < s.weights[min]) min = m;
					}

					if (s.weights[min] < weight)
					{
						s.weights[min] = weight;
						s.joints[min] = joint;
					}
				}
			}
		}

		for (Skin& s : skinning)
		{
			float sum = 0;
			for (float w : s.weights) sum += w;
			for (float& w : s.weights) w /= sum;
		}
	}


	Vec3 fixRootOrientation(const Vec3& v) const
	{
		switch (root_orientation)
		{
			case Orientation::Y_UP: return Vec3(v.x, v.y, v.z);
			case Orientation::Z_UP: return Vec3(v.x, v.z, -v.y);
			case Orientation::Z_MINUS_UP: return Vec3(v.x, -v.z, v.y);
			case Orientation::X_MINUS_UP: return Vec3(v.y, -v.x, v.z);
			case Orientation::X_UP: return Vec3(-v.y, v.x, v.z);
		}
		ASSERT(false);
		return Vec3(v.x, v.y, v.z);
	}


	Quat fixRootOrientation(const Quat& v) const
	{
		switch (root_orientation)
		{
			case Orientation::Y_UP: return Quat(v.x, v.y, v.z, v.w);
			case Orientation::Z_UP: return Quat(v.x, v.z, -v.y, v.w);
			case Orientation::Z_MINUS_UP: return Quat(v.x, -v.z, v.y, v.w);
			case Orientation::X_MINUS_UP: return Quat(v.y, -v.x, v.z, v.w);
			case Orientation::X_UP: return Quat(-v.y, v.x, v.z, v.w);
		}
		ASSERT(false);
		return Quat(v.x, v.y, v.z, v.w);
	}


	Vec3 fixOrientation(const Vec3& v) const
	{
		switch (orientation)
		{
			case Orientation::Y_UP: return Vec3(v.x, v.y, v.z);
			case Orientation::Z_UP: return Vec3(v.x, v.z, -v.y);
			case Orientation::Z_MINUS_UP: return Vec3(v.x, -v.z, v.y);
			case Orientation::X_MINUS_UP: return Vec3(v.y, -v.x, v.z);
			case Orientation::X_UP: return Vec3(-v.y, v.x, v.z);
		}
		ASSERT(false);
		return Vec3(v.x, v.y, v.z);
	}


	Quat fixOrientation(const Quat& v) const
	{
		switch (orientation)
		{
			case Orientation::Y_UP: return Quat(v.x, v.y, v.z, v.w);
			case Orientation::Z_UP: return Quat(v.x, v.z, -v.y, v.w);
			case Orientation::Z_MINUS_UP: return Quat(v.x, -v.z, v.y, v.w);
			case Orientation::X_MINUS_UP: return Quat(v.y, -v.x, v.z, v.w);
			case Orientation::X_UP: return Quat(-v.y, v.x, v.z, v.w);
		}
		ASSERT(false);
		return Quat(v.x, v.y, v.z, v.w);
	}


	void writeBillboardVertices(const AABB& aabb)
	{
		if (!create_billboard_lod) return;

		bool has_tangents = false;
		for (auto& mesh : meshes)
		{
			if (mesh.import)
			{
				has_tangents = mesh.fbx->getGeometry()->getTangents() != nullptr;
				break;
			}
		}

		Vec3 max = aabb.max;
		Vec3 min = aabb.min;
		Vec3 size = max - min;
		BillboardSceneData data({min, max}, BillboardSceneData::TEXTURE_SIZE);
		Matrix mtx = data.computeMVPMatrix();
		Vec3 uv0_min = mtx.transformPoint(min);
		Vec3 uv0_max = mtx.transformPoint(max);
		float x1_max = 0.0f;
		float x2_max = mtx.transformPoint(Vec3(max.x + size.z + size.x, 0, 0)).x;
		float x3_max = mtx.transformPoint(Vec3(max.x + size.z + size.x + size.z, 0, 0)).x;

		auto fixUV = [](float x, float y) -> Vec2 { return Vec2(x * 0.5f + 0.5f, y * 0.5f + 0.5f); };

		BillboardSceneData::Vertex vertices[] = {
			{{min.x, min.y, 0}, {128, 255, 128, 0}, {255, 128, 128, 0}, fixUV(uv0_min.x, uv0_max.y)},
			{{max.x, min.y, 0}, {128, 255, 128, 0}, {255, 128, 128, 0}, fixUV(uv0_max.x, uv0_max.y)},
			{{max.x, max.y, 0}, {128, 255, 128, 0}, {255, 128, 128, 0}, fixUV(uv0_max.x, uv0_min.y)},
			{{min.x, max.y, 0}, {128, 255, 128, 0}, {255, 128, 128, 0}, fixUV(uv0_min.x, uv0_min.y)},

			{{0, min.y, min.z}, {128, 255, 128, 0}, {128, 128, 255, 0}, fixUV(uv0_max.x, uv0_max.y)},
			{{0, min.y, max.z}, {128, 255, 128, 0}, {128, 128, 255, 0}, fixUV(x1_max, uv0_max.y)},
			{{0, max.y, max.z}, {128, 255, 128, 0}, {128, 128, 255, 0}, fixUV(x1_max, uv0_min.y)},
			{{0, max.y, min.z}, {128, 255, 128, 0}, {128, 128, 255, 0}, fixUV(uv0_max.x, uv0_min.y)},

			{{max.x, min.y, 0}, {128, 255, 128, 0}, {0, 128, 128, 0}, fixUV(x1_max, uv0_max.y)},
			{{min.x, min.y, 0}, {128, 255, 128, 0}, {0, 128, 128, 0}, fixUV(x2_max, uv0_max.y)},
			{{min.x, max.y, 0}, {128, 255, 128, 0}, {0, 128, 128, 0}, fixUV(x2_max, uv0_min.y)},
			{{max.x, max.y, 0}, {128, 255, 128, 0}, {0, 128, 128, 0}, fixUV(x1_max, uv0_min.y)},

			{{0, min.y, max.z}, {128, 255, 128, 0}, {128, 128, 0, 0}, fixUV(x2_max, uv0_max.y)},
			{{0, min.y, min.z}, {128, 255, 128, 0}, {128, 128, 0, 0}, fixUV(x3_max, uv0_max.y)},
			{{0, max.y, min.z}, {128, 255, 128, 0}, {128, 128, 0, 0}, fixUV(x3_max, uv0_min.y)},
			{{0, max.y, max.z}, {128, 255, 128, 0}, {128, 128, 0, 0}, fixUV(x2_max, uv0_min.y)}};

		int vertex_data_size = sizeof(BillboardSceneData::Vertex);
		if (!has_tangents) vertex_data_size -= 4;
		vertex_data_size *= lengthOf(vertices);
		write(vertex_data_size);
		for (const BillboardSceneData::Vertex& vertex : vertices)
		{
			write(vertex.pos);
			write(vertex.normal);
			if(has_tangents) write(vertex.tangent);
			write(vertex.uv);
		}
	}


	void writeGeometry()
	{
		AABB aabb = {{0, 0, 0}, {0, 0, 0}};
		float radius_squared = 0;
		IAllocator& allocator = app.getWorldEditor().getAllocator();

		OutputBlob vertices_blob(allocator);
		for (const ImportMesh& import_mesh : meshes)
		{
			if (!import_mesh.import) continue;
			bool are_indices_16_bit = areIndices16Bit(import_mesh);
			if (are_indices_16_bit)
			{
				int index_size = sizeof(u16);
				write(index_size);
				write(import_mesh.indices.size());
				for (int i : import_mesh.indices)
				{
					assert(i <= (1 << 16));
					u16 index = (u16)i;
					write(index);
				}
			}
			else
			{
				int index_size = sizeof(import_mesh.indices[0]);
				write(index_size);
				write(import_mesh.indices.size());
				write(&import_mesh.indices[0], sizeof(import_mesh.indices[0]) * import_mesh.indices.size());
			}
			aabb.merge(import_mesh.aabb);
			radius_squared = Math::maximum(radius_squared, import_mesh.radius_squared);
		}

		if (create_billboard_lod)
		{
			u16 indices[] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15 };
			write(indices, sizeof(indices));
		}

		for (const ImportMesh& import_mesh : meshes)
		{
			if (!import_mesh.import) continue;
			write(import_mesh.vertex_data.getPos());
			write(import_mesh.vertex_data.getData(), import_mesh.vertex_data.getPos());
		}
		writeBillboardVertices(aabb);

		write(sqrtf(radius_squared) * bounding_shape_scale);
		aabb.min *= bounding_shape_scale;
		aabb.max *= bounding_shape_scale;
		write(aabb);
	}


	void writeBillboardMesh(i32 attribute_array_offset, i32 indices_offset, const char* mesh_output_filename)
	{
		if (!create_billboard_lod) return;
		
		StaticString<MAX_PATH_LENGTH + 10> material_name(mesh_output_filename, "_billboard");
		i32 length = stringLength(material_name);
		write((const char*)&length, sizeof(length));
		write(material_name, length);

		const char* mesh_name = "billboard";
		length = stringLength(mesh_name);

		write((const char*)&length, sizeof(length));
		write(mesh_name, length);
	}


	static void getMaterialName(const ofbx::Material* material, char (&out)[128])
	{
		copyString(out, material ? material->name : "default");
		char* iter = out;
		while (*iter)
		{
			char c = *iter;
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
			{
				*iter = '_';
			}
			++iter;
		}
	}


	void writeMeshes(const char* mesh_output_filename)
	{
		i32 mesh_count = 0;
		for (ImportMesh& mesh : meshes)
			if (mesh.import) ++mesh_count;
		if (create_billboard_lod) ++mesh_count;
		write(mesh_count);

		i32 attr_offset = 0;
		i32 indices_offset = 0;
		for (ImportMesh& import_mesh : meshes)
		{
			if (!import_mesh.import) continue;
			
			const ofbx::Mesh& mesh = *import_mesh.fbx;

			i32 attribute_count = getAttributeCount(mesh);
			write(attribute_count);

			i32 pos_attr = 0;
			write(pos_attr);
			const ofbx::Geometry* geom = mesh.getGeometry();
			if (geom->getNormals())
			{
				i32 nrm_attr = 1;
				write(nrm_attr);
			}
			if (geom->getUVs())
			{
				i32 uv0_attr = 8;
				write(uv0_attr);
			}
			if (geom->getColors() && import_vertex_colors)
			{
				i32 color_attr = 4;
				write(color_attr);
			}
			if (geom->getTangents())
			{
				i32 color_attr = 2;
				write(color_attr);
			}
			if (isSkinned(mesh))
			{
				i32 indices_attr = 6;
				write(indices_attr);
				i32 weight_attr = 7;
				write(weight_attr);
			}

			const ofbx::Material* material = import_mesh.fbx_mat;
			char mat[128];
			getMaterialName(material, mat);
			i32 mat_len = (i32)strlen(mat);
			write(mat_len);
			write(mat, strlen(mat));

			const char* name = getImportMeshName(import_mesh);
			i32 name_len = (i32)strlen(name);
			write(name_len);
			write(name, strlen(name));
		}

		writeBillboardMesh(attr_offset, indices_offset, mesh_output_filename);
	}


	void writeSkeleton()
	{
		if (ignore_skeleton)
		{
			write((int)0);
			return;
		}

		write(bones.size());

		for (const ofbx::Object* node : bones)
		{
			const char* name = node->name;
			int len = (int)strlen(name);
			write(len);
			writeString(name);

			ofbx::Object* parent = node->getParent();
			if (!parent)
			{
				write((int)0);
			}
			else
			{
				const char* parent_name = parent->name;
				len = (int)strlen(parent_name);
				write(len);
				writeString(parent_name);
			}

			const ofbx::Mesh* mesh = getAnyMeshFromBone(node);
			Matrix tr = toLumix(getBindPoseMatrix(mesh, node));
			tr.normalizeScale();

			Quat q = fixOrientation(tr.getRotation());
			Vec3 t = fixOrientation(tr.getTranslation());
			write(t * mesh_scale);
			write(q);
		}
	}


	void writeLODs()
	{
		i32 lod_count = 1;
		i32 last_mesh_idx = -1;
		i32 lods[8] = {};
		for (auto& mesh : meshes)
		{
			if (!mesh.import) continue;

			++last_mesh_idx;
			if (mesh.lod >= lengthOf(lods_distances)) continue;
			lod_count = mesh.lod + 1;
			lods[mesh.lod] = last_mesh_idx;
		}

		for (int i = 1; i < Lumix::lengthOf(lods); ++i)
		{
			if (lods[i] < lods[i - 1]) lods[i] = lods[i - 1];
		}

		if (create_billboard_lod)
		{
			lods[lod_count] = last_mesh_idx + 1;
			++lod_count;
		}

		write((const char*)&lod_count, sizeof(lod_count));

		for (int i = 0; i < lod_count; ++i)
		{
			i32 to_mesh = lods[i];
			write((const char*)&to_mesh, sizeof(to_mesh));
			float factor = lods_distances[i] < 0 ? FLT_MAX : lods_distances[i] * lods_distances[i];
			write((const char*)&factor, sizeof(factor));
		}
	}


	int getAttributeCount(const ofbx::Mesh& mesh) const
	{
		int count = 1; // position
		if (mesh.getGeometry()->getNormals()) ++count;
		if (mesh.getGeometry()->getUVs()) ++count;
		if (mesh.getGeometry()->getColors() && import_vertex_colors) ++count;
		if (mesh.getGeometry()->getTangents()) ++count;
		if (isSkinned(mesh)) count += 2;
		return count;
	}


	bool areIndices16Bit(const ImportMesh& mesh) const
	{
		int vertex_size = getVertexSize(*mesh.fbx);
		return !(mesh.import && mesh.vertex_data.getPos() / vertex_size > (1 << 16));
	}


	void writeModelHeader()
	{
		Model::FileHeader header;
		header.magic = 0x5f4c4d4f; // == '_LMO';
		header.version = (u32)Model::FileVersion::LATEST;
		write(header);
		u32 flags = 0;
		write(flags);
	}


	void writePhysicsHeader(FS::OsFile& file) const
	{
		PhysicsGeometry::Header header;
		header.m_magic = PhysicsGeometry::HEADER_MAGIC;
		header.m_version = (u32)PhysicsGeometry::Versions::LAST;
		header.m_convex = (u32)make_convex;
		file.write((const char*)&header, sizeof(header));
	}


	void writePhysicsTriMesh(FS::OsFile& file)
	{
		i32 count = 0;
		for (auto& mesh : meshes)
		{
			if (mesh.import_physics) count += mesh.indices.size();
		}
		file.write((const char*)&count, sizeof(count));
		int offset = 0;
		for (auto& mesh : meshes)
		{
			if (!mesh.import_physics) continue;
			for (unsigned int j = 0, c = mesh.indices.size(); j < c; ++j)
			{
				u32 index = mesh.indices[j] + offset;
				file.write((const char*)&index, sizeof(index));
			}
			int vertex_size = getVertexSize(*mesh.fbx);
			int vertex_count = (i32)(mesh.vertex_data.getPos() / vertex_size);
			offset += vertex_count;
		}
	}


	bool writePhysics()
	{
		bool any = false;
		for (const ImportMesh& m : meshes)
		{
			if (m.import_physics)
			{
				any = true;
				break;
			}
		}

		if (!any) return true;

		dialog.setImportMessage("Importing physics...", -1);
		char filename[MAX_PATH_LENGTH];
		PathUtils::getBasename(filename, sizeof(filename), dialog.m_source);
		catString(filename, ".phy");
		PathBuilder phy_path(dialog.m_output_dir);
		PlatformInterface::makePath(phy_path);
		phy_path << "/" << filename;
		FS::OsFile file;
		if (!file.open(phy_path, FS::Mode::CREATE_AND_WRITE))
		{
			g_log_error.log("Editor") << "Could not create file " << phy_path;
			return false;
		}

		writePhysicsHeader(file);
		i32 count = 0;
		for (auto& mesh : meshes)
		{
			if (mesh.import_physics) count += (i32)(mesh.vertex_data.getPos() / getVertexSize(*mesh.fbx));
		}
		file.write((const char*)&count, sizeof(count));
		for (auto& mesh : meshes)
		{
			if (mesh.import_physics)
			{
				int vertex_size = getVertexSize(*mesh.fbx);
				int vertex_count = (i32)(mesh.vertex_data.getPos() / vertex_size);

				const u8* verts = (const u8*)mesh.vertex_data.getData();

				for (int i = 0; i < vertex_count; ++i)
				{
					Vec3 v = *(Vec3*)(verts + i * vertex_size);
					file.write(&v, sizeof(v));
				}
			}
		}

		if (!make_convex) writePhysicsTriMesh(file);
		file.close();

		return true;
	}


	bool save(const char* output_dir, const char* output_mesh_filename, const char* texture_output_dir)
	{
		writeModel(output_dir, output_mesh_filename);
		writeAnimations(output_dir);
		writeMaterials(output_dir, texture_output_dir, output_mesh_filename);
		writePhysics();

		return true;
	}


	void writeModel(const char* output_dir, const char* output_mesh_filename)
	{
		postprocessMeshes();

		auto cmpMeshes = [](const void* a, const void* b) -> int {
			auto a_mesh = static_cast<const ImportMesh*>(a);
			auto b_mesh = static_cast<const ImportMesh*>(b);
			return a_mesh->lod - b_mesh->lod;
		};

		bool import_any_mesh = false;
		for (const ImportMesh& m : meshes)
		{
			if (m.import) import_any_mesh = true;
		}
		if (!import_any_mesh) return;

		qsort(&meshes[0], meshes.size(), sizeof(meshes[0]), cmpMeshes);
		StaticString<MAX_PATH_LENGTH> out_path(output_dir, output_mesh_filename, ".msh");
		PlatformInterface::makePath(output_dir);
		if (!out_file.open(out_path, FS::Mode::CREATE_AND_WRITE))
		{
			g_log_error.log("FBX") << "Failed to create " << out_path;
			return;
		}

		dialog.setImportMessage("Writing model...", 0.5f);
		writeModelHeader();
		writeMeshes(output_mesh_filename);
		writeGeometry();
		writeSkeleton();
		writeLODs();
		out_file.close();
	}


	void clearSources()
	{
		for (ofbx::IScene* scene : scenes) scene->destroy();
		scenes.clear();
		meshes.clear();
		materials.clear();
		animations.clear();
		bones.clear();
	}


	void toggleOpen() { open = !open; }
	bool isOpen() const { return open; }

	
	static const char* getImportMeshName(const ImportMesh& mesh)
	{
		const char* name = mesh.fbx->name;
		const ofbx::Material* material = mesh.fbx_mat;

		if (name[0] == '\0' && mesh.fbx->getParent()) name = mesh.fbx->getParent()->name;
		if (name[0] == '\0' && material) name = material->name;
		return name;
	}


	StudioApp& app;
	ImportAssetDialog& dialog;
	bool open = false;
	Array<ImportMaterial> materials;
	Array<ImportMesh> meshes;
	Array<ImportAnimation> animations;
	Array<const ofbx::Object*> bones;
	Array<ofbx::IScene*> scenes;
	float lods_distances[4] = {-10, -100, -1000, -10000};
	FS::OsFile out_file;
	float mesh_scale = 1.0f;
	float time_scale = 1.0f;
	float position_error = 0.1f;
	float rotation_error = 0.01f;
	float bounding_shape_scale = 1.0f;
	bool to_dds = false;
	bool center_mesh = false;
	bool ignore_skeleton = false;
	bool import_vertex_colors = true;
	bool make_convex = false;
	bool create_billboard_lod = false;
	Orientation orientation = Orientation::Y_UP;
	Orientation root_orientation = Orientation::Y_UP;
};


enum class VertexAttributeDef : u32
{
	POSITION,
	FLOAT1,
	FLOAT2,
	FLOAT3,
	FLOAT4,
	INT1,
	INT2,
	INT3,
	INT4,
	SHORT2,
	SHORT4,
	BYTE4,
	NONE
};


static void resizeImage(ImportAssetDialog* dlg, int new_w, int new_h)
{
	ImportAssetDialog::ImageData& img = dlg->m_image;
	u8* mem = (u8*)stbi__malloc(new_w * new_h * 4);
	stbir_resize_uint8(img.data,
		img.width,
		img.height,
		0,
		mem,
		new_w,
		new_h,
		0,
		4);

	stbi_image_free(img.data);
	img.data = mem;
	img.width = new_w;
	img.height = new_h;
}


namespace LuaAPI
{


int setMeshParams(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	
	int mesh_idx = LuaWrapper::checkArg<int>(L, 1);
	LuaWrapper::checkTableArg(L, 2);
	if (mesh_idx < 0 || mesh_idx >= dlg->m_fbx_importer->meshes.size()) return 0;
	
	auto& mesh = dlg->m_fbx_importer->meshes[mesh_idx];

	LuaWrapper::getOptionalField(L, 2, "lod", &mesh.lod);
	LuaWrapper::getOptionalField(L, 2, "import", &mesh.import);
	LuaWrapper::getOptionalField(L, 2, "import_physics", &mesh.import_physics);

	return 0;
}


int setAnimationParams(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));

	int anim_idx = LuaWrapper::checkArg<int>(L, 1);
	LuaWrapper::checkTableArg(L, 2);
	if (anim_idx < 0 || anim_idx >= dlg->m_fbx_importer->animations.size()) return 0;

	auto& anim = dlg->m_fbx_importer->animations[anim_idx];

	if (lua_getfield(L, 2, "root_bone") == LUA_TSTRING)
	{
		const char* name = lua_tostring(L, -1);
		auto* layer = anim.fbx->getLayer(0);
		for (int i = 0; i < dlg->m_fbx_importer->bones.size(); ++i)
		{
			if (equalStrings(dlg->m_fbx_importer->bones[i]->name, name))
			{
				anim.root_motion_bone_idx = i;
				break;
			}
		}
	}
	lua_pop(L, 1); // "root_bone"

	LuaWrapper::getOptionalField(L, 2, "import", &anim.import);

	if (lua_getfield(L, 2, "output_filename") == LUA_TSTRING)
	{
		copyString(anim.output_filename.data, LuaWrapper::toType<const char*>(L, -1));
	}
	lua_pop(L, 1);

	return 0;
}


int setParams(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	LuaWrapper::checkTableArg(L, 1);

	if (lua_getfield(L, 1, "output_dir") == LUA_TSTRING)
	{
		copyString(dlg->m_output_dir, LuaWrapper::toType<const char*>(L, -1));
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 1, "mesh_output_filename") == LUA_TSTRING)
	{
		copyString(dlg->m_mesh_output_filename, LuaWrapper::toType<const char*>(L, -1));
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 1, "texture_output_dir") == LUA_TSTRING)
	{
		copyString(dlg->m_texture_output_dir, LuaWrapper::toType<const char*>(L, -1));
	}
	lua_pop(L, 1);

	LuaWrapper::getOptionalField(L, 1, "create_billboard", &dlg->m_fbx_importer->create_billboard_lod);
	LuaWrapper::getOptionalField(L, 1, "center_meshes", &dlg->m_fbx_importer->center_mesh);
	LuaWrapper::getOptionalField(L, 1, "import_vertex_colors", &dlg->m_fbx_importer->import_vertex_colors);
	LuaWrapper::getOptionalField(L, 1, "scale", &dlg->m_fbx_importer->mesh_scale);
	LuaWrapper::getOptionalField(L, 1, "time_scale", &dlg->m_fbx_importer->time_scale);
	LuaWrapper::getOptionalField(L, 1, "to_dds", &dlg->m_convert_to_dds);
	LuaWrapper::getOptionalField(L, 1, "normal_map", &dlg->m_is_normal_map);
	if (lua_getfield(L, 1, "orientation") == LUA_TSTRING)
	{
		const char* tmp = LuaWrapper::toType<const char*>(L, -1);
		if (equalStrings(tmp, "+y")) dlg->m_fbx_importer->orientation = FBXImporter::Orientation::Y_UP;
		else if (equalStrings(tmp, "+z")) dlg->m_fbx_importer->orientation = FBXImporter::Orientation::Z_UP;
		else if (equalStrings(tmp, "-y")) dlg->m_fbx_importer->orientation = FBXImporter::Orientation::X_MINUS_UP;
		else if (equalStrings(tmp, "-z")) dlg->m_fbx_importer->orientation = FBXImporter::Orientation::Z_MINUS_UP;
	}
	lua_pop(L, 1);
	if (lua_getfield(L, 1, "root_orientation") == LUA_TSTRING)
	{
		const char* tmp = LuaWrapper::toType<const char*>(L, -1);
		if (equalStrings(tmp, "+y")) dlg->m_fbx_importer->root_orientation = FBXImporter::Orientation::Y_UP;
		else if (equalStrings(tmp, "+z")) dlg->m_fbx_importer->root_orientation = FBXImporter::Orientation::Z_UP;
		else if (equalStrings(tmp, "-y")) dlg->m_fbx_importer->root_orientation = FBXImporter::Orientation::X_MINUS_UP;
		else if (equalStrings(tmp, "-z")) dlg->m_fbx_importer->root_orientation = FBXImporter::Orientation::Z_MINUS_UP;
	}
	lua_pop(L, 1);


	if (lua_getfield(L, 1, "lods") == LUA_TTABLE)
	{
		lua_pushnil(L);
		int lod_index = 0;
		while (lua_next(L, -2) != 0)
		{
			if (lod_index >= lengthOf(dlg->m_fbx_importer->lods_distances))
			{
				g_log_error.log("Editor") << "Only " << lengthOf(dlg->m_fbx_importer->lods_distances) << " supported";
				lua_pop(L, 1);
				break;
			}

			dlg->m_fbx_importer->lods_distances[lod_index] = LuaWrapper::toType<float>(L, -1);
			++lod_index;
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	return 0;
}


int setTextureParams(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	int material_idx = LuaWrapper::checkArg<int>(L, 1);
	int texture_idx = LuaWrapper::checkArg<int>(L, 2);
	LuaWrapper::checkTableArg(L, 3);
	
	if (material_idx < 0 || material_idx >= dlg->m_fbx_importer->materials.size()) return 0;
	auto& material = dlg->m_fbx_importer->materials[material_idx];
	
	if (texture_idx < 0 || texture_idx >= lengthOf(material.textures)) return 0;
	auto& texture = material.textures[texture_idx];

	LuaWrapper::getOptionalField(L, 3, "import", &texture.import);
	LuaWrapper::getOptionalField(L, 3, "to_dds", &texture.to_dds);

	return 0;
}


int setMaterialParams(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	int material_idx = LuaWrapper::checkArg<int>(L, 1);
	LuaWrapper::checkTableArg(L, 2);
	if (material_idx < 0 || material_idx >= dlg->m_fbx_importer->materials.size()) return 0;

	auto& material = dlg->m_fbx_importer->materials[material_idx];

	LuaWrapper::getOptionalField(L, 2, "import", &material.import);
	LuaWrapper::getOptionalField(L, 2, "alpha_cutout", &material.alpha_cutout);

	return 0;
}


int getMeshesCount(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	return dlg->m_fbx_importer->meshes.size();
}


int getAnimationsCount(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	return dlg->m_fbx_importer->animations.size();
}


const char* getMeshMaterialName(lua_State* L, int mesh_idx)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	if (mesh_idx < 0 || mesh_idx >= dlg->m_fbx_importer->meshes.size()) return "";
	return dlg->m_fbx_importer->meshes[mesh_idx].fbx_mat->name;
}


int getImageWidth(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	return dlg->m_image.width;
}


void resizeImage(lua_State* L, int new_w, int new_h)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	resizeImage(dlg, new_w, new_h);
}


int getImageHeight(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	return dlg->m_image.height;
}


int getMaterialsCount(lua_State* L)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	return dlg->m_fbx_importer->materials.size();
}


const char* getMeshName(lua_State* L, int mesh_idx)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	if (mesh_idx < 0 || mesh_idx >= dlg->m_fbx_importer->meshes.size()) return "";
	return dlg->m_fbx_importer->meshes[mesh_idx].fbx->name;
}


const char* getMaterialName(lua_State* L, int material_idx)
{
	auto* dlg = LuaWrapper::toType<ImportAssetDialog*>(L, lua_upvalueindex(1));
	if (material_idx < 0 || material_idx >= dlg->m_fbx_importer->materials.size()) return "";
	return dlg->m_fbx_importer->materials[material_idx].fbx->name;
}

} // namespace LuaAPI


static void getRelativePath(WorldEditor& editor, char* relative_path, int max_length, const char* source)
{
	char tmp[MAX_PATH_LENGTH];
	PathUtils::normalize(source, tmp, sizeof(tmp));

	const char* base_path = editor.getEngine().getDiskFileDevice()->getBasePath();
	if (compareStringN(base_path, tmp, stringLength(base_path)) == 0)
	{
		int base_path_length = stringLength(base_path);
		const char* rel_path_start = tmp + base_path_length;
		if (rel_path_start[0] == '/')
		{
			++rel_path_start;
		}
		copyString(relative_path, max_length, rel_path_start);
	}
	else
	{
		auto* patch_fd = editor.getEngine().getPatchFileDevice();
		const char* base_path = patch_fd ? patch_fd->getBasePath() : nullptr;
		if (base_path && compareStringN(base_path, tmp, stringLength(base_path)) == 0)
		{
			int base_path_length = stringLength(base_path);
			const char* rel_path_start = tmp + base_path_length;
			if (rel_path_start[0] == '/')
			{
				++rel_path_start;
			}
			copyString(relative_path, max_length, rel_path_start);
		}
		else
		{
			copyString(relative_path, max_length, tmp);
		}

	}
}


static crn_bool ddsConvertCallback(crn_uint32 phase_index,
	crn_uint32 total_phases,
	crn_uint32 subphase_index,
	crn_uint32 total_subphases,
	void* pUser_data_ptr)
{
	auto* data = (ImportAssetDialog::DDSConvertCallbackData*)pUser_data_ptr;

	float fraction = phase_index / float(total_phases) + (subphase_index / float(total_subphases)) / total_phases;
	data->dialog->setImportMessage(
		StaticString<MAX_PATH_LENGTH + 50>("Saving ", data->dest_path), fraction);

	return !data->cancel_requested;
}


static bool saveAsRaw(ImportAssetDialog& dialog,
	FS::FileSystem& fs,
	const u8* image_data,
	int image_width,
	int image_height,
	const char* dest_path,
	float scale,
	IAllocator& allocator)
{
	ASSERT(image_data);

	dialog.setImportMessage(StaticString<MAX_PATH_LENGTH + 30>("Saving ") << dest_path, -1);

	FS::OsFile file;
	if (!file.open(dest_path, FS::Mode::CREATE_AND_WRITE))
	{
		dialog.setMessage(StaticString<MAX_PATH_LENGTH + 30>("Could not save ") << dest_path);
		return false;
	}

	Array<u16> data(allocator);
	data.resize(image_width * image_height);
	for (int j = 0; j < image_height; ++j)
	{
		for (int i = 0; i < image_width; ++i)
		{
			data[i + j * image_width] = u16(scale * image_data[(i + j * image_width) * 4]);
		}
	}

	file.write((const char*)&data[0], data.size() * sizeof(data[0]));
	file.close();
	return true;
}


static bool saveAsDDS(ImportAssetDialog& dialog,
	const char* source_path,
	const u8* image_data,
	int image_width,
	int image_height,
	bool alpha,
	bool normal,
	const char* dest_path)
{
	ASSERT(image_data);

	dialog.setImportMessage(StaticString<MAX_PATH_LENGTH + 30>("Saving ") << dest_path, 0);

	dialog.getDDSConvertCallbackData().dialog = &dialog;
	dialog.getDDSConvertCallbackData().dest_path = dest_path;
	dialog.getDDSConvertCallbackData().cancel_requested = false;

	crn_uint32 size;
	crn_comp_params comp_params = s_default_comp_params;
	comp_params.m_width = image_width;
	comp_params.m_height = image_height;
	comp_params.m_format = normal ? cCRNFmtDXN_YX : (alpha ? cCRNFmtDXT5 : cCRNFmtDXT1);
	comp_params.m_pImages[0][0] = (u32*)image_data;
	crn_mipmap_params mipmap_params;
	mipmap_params.m_mode = cCRNMipModeGenerateMips;

	void* data = crn_compress(comp_params, mipmap_params, size);
	if (!data)
	{
		dialog.setMessage(StaticString<MAX_PATH_LENGTH + 30>("Could not convert ") << source_path);
		return false;
	}

	FS::OsFile file;
	if (!file.open(dest_path, FS::Mode::CREATE_AND_WRITE))
	{
		dialog.setMessage(StaticString<MAX_PATH_LENGTH + 30>("Could not save ") << dest_path);
		crn_free_block(data);
		return false;
	}

	file.write((const char*)data, size);
	file.close();
	crn_free_block(data);
	return true;
}


struct ImportTextureTask LUMIX_FINAL : public MT::Task
{
	explicit ImportTextureTask(ImportAssetDialog& dialog)
		: Task(dialog.m_editor.getAllocator())
		, m_dialog(dialog)
	{
	}


	static void getDestinationPath(const char* output_dir,
		const char* source,
		bool to_dds,
		bool to_raw,
		char* out,
		int max_size)
	{
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(basename, sizeof(basename), source);

		if (to_dds)
		{
			PathBuilder dest_path(output_dir);
			dest_path << "/" << basename << ".dds";
			copyString(out, max_size, dest_path);
			return;
		}

		if (to_raw)
		{
			PathBuilder dest_path(output_dir);
			dest_path << "/" << basename << ".raw";
			copyString(out, max_size, dest_path);
			return;
		}

		char ext[MAX_PATH_LENGTH];
		PathUtils::getExtension(ext, sizeof(ext), source);
		PathBuilder dest_path(output_dir);
		dest_path << "/" << basename << "." << ext;
		copyString(out, max_size, dest_path);
	}


	int task() override
	{
		m_dialog.setImportMessage("Importing texture...", 0);

		if (!m_dialog.m_image.data)
		{
			m_dialog.setMessage(StaticString<MAX_PATH_LENGTH + 200>("Could not load ") << m_dialog.m_source << " : "
																					   << stbi_failure_reason());
			return -1;
		}

		char dest_path[MAX_PATH_LENGTH];
		getDestinationPath(m_dialog.m_output_dir,
			m_dialog.m_source,
			m_dialog.m_convert_to_dds,
			m_dialog.m_convert_to_raw,
			dest_path,
			lengthOf(dest_path));

		if (m_dialog.m_convert_to_dds)
		{
			m_dialog.setImportMessage("Converting to DDS...", 0);

			saveAsDDS(m_dialog,
				m_dialog.m_source,
				m_dialog.m_image.data,
				m_dialog.m_image.width,
				m_dialog.m_image.height,
				m_dialog.m_image.comps == 4,
				m_dialog.m_is_normal_map,
				dest_path);
		}
		else if (m_dialog.m_convert_to_raw)
		{
			m_dialog.setImportMessage("Converting to RAW...", -1);

			saveAsRaw(m_dialog,
				m_dialog.m_editor.getEngine().getFileSystem(),
				m_dialog.m_image.data,
				m_dialog.m_image.width,
				m_dialog.m_image.height,
				dest_path,
				m_dialog.m_raw_texture_scale,
				m_dialog.m_editor.getAllocator());
		}
		else
		{
			m_dialog.setImportMessage("Copying...", -1);

			if (!copyFile(m_dialog.m_source, dest_path))
			{
				m_dialog.setMessage(StaticString<MAX_PATH_LENGTH * 2 + 30>("Could not copy ")
									<< m_dialog.m_source
									<< " to "
									<< dest_path);
			}
		}
		stbi_image_free(m_dialog.m_image.data);
		m_dialog.m_image.data = nullptr;

		return 0;
	}


	ImportAssetDialog& m_dialog;

}; // struct ImportTextureTask



ImportAssetDialog::ImportAssetDialog(StudioApp& app)
	: m_metadata(app.getMetadata())
	, m_task(nullptr)
	, m_editor(app.getWorldEditor())
	, m_is_importing_texture(false)
	, m_mutex(false)
	, m_saved_textures(app.getWorldEditor().getAllocator())
	, m_convert_to_dds(false)
	, m_convert_to_raw(false)
	, m_is_normal_map(false)
	, m_raw_texture_scale(1)
	, m_sources(app.getWorldEditor().getAllocator())
{
	IAllocator& allocator = app.getWorldEditor().getAllocator();
	m_fbx_importer = LUMIX_NEW(allocator, FBXImporter)(app, *this);

	s_default_comp_params.m_file_type = cCRNFileTypeDDS;
	s_default_comp_params.m_quality_level = cCRNMaxQualityLevel;
	s_default_comp_params.m_dxt_quality = cCRNDXTQualityNormal;
	s_default_comp_params.m_dxt_compressor_type = cCRNDXTCompressorCRN;
	s_default_comp_params.m_pProgress_func = ddsConvertCallback;
	s_default_comp_params.m_pProgress_func_data = &m_dds_convert_callback;
	s_default_comp_params.m_num_helper_threads = 3;

	m_image.data = nullptr;

	m_is_open = false;
	m_message[0] = '\0';
	m_import_message[0] = '\0';
	m_task = nullptr;
	m_source[0] = '\0';
	m_output_dir[0] = '\0';
	m_mesh_output_filename[0] = '\0';
	m_texture_output_dir[0] = '\0';
	copyString(m_last_dir, m_editor.getEngine().getDiskFileDevice()->getBasePath());

	Action* action = LUMIX_NEW(m_editor.getAllocator(), Action)("Import Asset", "import_asset");
	action->func.bind<ImportAssetDialog, &ImportAssetDialog::onAction>(this);
	action->is_selected.bind<ImportAssetDialog, &ImportAssetDialog::isOpen>(this);
	app.addWindowAction(action);

	lua_State* L = m_editor.getEngine().getState();

	#define REGISTER_FUNCTION(name) \
		do {\
			auto f = &LuaWrapper::wrapMethodClosure<ImportAssetDialog, decltype(&ImportAssetDialog::name), &ImportAssetDialog::name>; \
			LuaWrapper::createSystemClosure(L, "ImportAsset", this, #name, f); \
		} while(false) \

	REGISTER_FUNCTION(clearSources);
	REGISTER_FUNCTION(addSource);
	REGISTER_FUNCTION(import);
	REGISTER_FUNCTION(importTexture);
	REGISTER_FUNCTION(checkTask);

	#undef REGISTER_FUNCTION

	#define REGISTER_FUNCTION(name) \
		do {\
			auto f = &LuaWrapper::wrap<decltype(&LuaAPI::name), &LuaAPI::name>; \
			LuaWrapper::createSystemClosure(L, "ImportAsset", this, #name, f); \
		} while(false) \

	REGISTER_FUNCTION(getMeshesCount);
	REGISTER_FUNCTION(getAnimationsCount);
	REGISTER_FUNCTION(getMeshMaterialName);
	REGISTER_FUNCTION(getMaterialsCount);
	REGISTER_FUNCTION(getMeshName);
	REGISTER_FUNCTION(getMaterialName);
	REGISTER_FUNCTION(getImageWidth);
	REGISTER_FUNCTION(getImageHeight);
	REGISTER_FUNCTION(resizeImage);

	#undef REGISTER_FUNCTION

	#define REGISTER_FUNCTION(name) \
		do {\
			LuaWrapper::createSystemClosure(L, "ImportAsset", this, #name, &LuaAPI::name); \
		} while(false) \

	REGISTER_FUNCTION(setParams);
	REGISTER_FUNCTION(setMeshParams);
	REGISTER_FUNCTION(setMaterialParams);
	REGISTER_FUNCTION(setTextureParams);
	REGISTER_FUNCTION(setAnimationParams);

	#undef REGISTER_FUNCTION
}


bool ImportAssetDialog::isOpen() const
{
	return m_is_open;
}


ImportAssetDialog::~ImportAssetDialog()
{
	if (m_task)
	{
		m_task->destroy();
		LUMIX_DELETE(m_editor.getAllocator(), m_task);
		m_task = nullptr;
	}
	clearSources();
	LUMIX_DELETE(m_editor.getAllocator(), m_fbx_importer);
}


static bool isImage(const char* path)
{
	char ext[10];
	PathUtils::getExtension(ext, sizeof(ext), path);

	static const char* image_extensions[] = {
		"dds", "jpg", "jpeg", "png", "tga", "bmp", "psd", "gif", "hdr", "pic", "pnm"};
	makeLowercase(ext, lengthOf(ext), ext);
	for (auto image_ext : image_extensions)
	{
		if (equalStrings(ext, image_ext))
		{
			return true;
		}
	}
	return false;
}


bool ImportAssetDialog::checkSource()
{
	if (!PlatformInterface::fileExists(m_source)) return false;
	if (m_output_dir[0] == '\0') PathUtils::getDir(m_output_dir, sizeof(m_output_dir), m_source);
	if (m_mesh_output_filename[0] == '\0') PathUtils::getBasename(m_mesh_output_filename, sizeof(m_mesh_output_filename), m_source);

	if (isImage(m_source))
	{
		stbi_image_free(m_image.data);
		m_image.data = stbi_load(m_source, &m_image.width, &m_image.height, &m_image.comps, 4);
		m_image.resize_size[0] = m_image.width;
		m_image.resize_size[1] = m_image.height;
		return m_image.data != nullptr;
	}

	stbi_image_free(m_image.data);
	m_image.data = nullptr;

	IAllocator& allocator = m_editor.getAllocator();

	ASSERT(!m_task);
	m_sources.emplace(m_source);
	setImportMessage("Importing...", -1);
	m_task = makeTask([this]() { m_fbx_importer->addSource(m_source); }, [](){}, allocator);
	m_task->create("Import mesh");
	return true;
}


void ImportAssetDialog::setMessage(const char* message)
{
	MT::SpinLock lock(m_mutex);
	copyString(m_message, message);
}


bool ImportAssetDialog::onDropFile(const char* file)
{
	if (PathUtils::hasExtension(file, "fbx") || PathUtils::hasExtension(file, "tga") || PathUtils::hasExtension(file, "dds"))
	{
		m_is_open = true;
		addSource(file);
		return true;
	}
	return false;
}


void ImportAssetDialog::setImportMessage(const char* message, float progress_fraction)
{
	MT::SpinLock lock(m_mutex);
	copyString(m_import_message, message);
	m_progress_fraction = progress_fraction;
}


void ImportAssetDialog::getMessage(char* msg, int max_size)
{
	MT::SpinLock lock(m_mutex);
	copyString(msg, max_size, m_message);
}


bool ImportAssetDialog::hasMessage()
{
	MT::SpinLock lock(m_mutex);
	return m_message[0] != '\0';
}


void ImportAssetDialog::saveModelMetadata()
{
	if (m_sources.empty()) return;

	PathBuilder model_path(m_output_dir, "/", m_mesh_output_filename, ".msh");
	char tmp[MAX_PATH_LENGTH];
	PathUtils::normalize(model_path, tmp, lengthOf(tmp));
	u32 model_path_hash = crc32(tmp);

	OutputBlob blob(m_editor.getAllocator());
	blob.reserve(1024);
	blob.write(m_fbx_importer->meshes.size());
	for (auto& i : m_fbx_importer->meshes)
	{
		blob.write(i.import);
		blob.write(i.import_physics);
		blob.write(i.lod);
	}
	blob.write(m_fbx_importer->materials.size());
	for (auto& i : m_fbx_importer->materials)
	{
		blob.write(i.import);
		blob.write(i.alpha_cutout);
		blob.write(i.shader);
		blob.write(lengthOf(i.textures));
		for (int j = 0; j < lengthOf(i.textures); ++j)
		{
			auto& texture = i.textures[j];
			blob.write(texture.import);
			blob.write(texture.path);
			blob.write(texture.src);
			blob.write(texture.to_dds);
		}
	}
	int sources_count = m_sources.size();
	blob.write(sources_count);
	blob.write(&m_sources[0], sizeof(m_sources) * m_sources.size());
	m_metadata.setRawMemory(model_path_hash, crc32("import_settings"), blob.getData(), blob.getPos());
}


void ImportAssetDialog::importTexture()
{
	ASSERT(!m_task);
	setImportMessage("Importing texture...", 0);

	char dest_path[MAX_PATH_LENGTH];
	ImportTextureTask::getDestinationPath(
		m_output_dir, m_source, m_convert_to_dds, m_convert_to_raw, dest_path, lengthOf(dest_path));

	char tmp[MAX_PATH_LENGTH];
	PathUtils::normalize(dest_path, tmp, lengthOf(tmp));
	getRelativePath(m_editor, dest_path, lengthOf(dest_path), tmp);
	u32 hash = crc32(dest_path);

	m_metadata.setString(hash, crc32("source"), m_source);

	m_is_importing_texture = true;
	m_task = LUMIX_NEW(m_editor.getAllocator(), ImportTextureTask)(*this);
	m_task->create("ImportTextureTask");
}


bool ImportAssetDialog::isTextureDirValid() const
{
	if (!m_texture_output_dir[0]) return true;
	char normalized_path[MAX_PATH_LENGTH];
	PathUtils::normalize(m_texture_output_dir, normalized_path, lengthOf(normalized_path));

	const char* base_path = m_editor.getEngine().getDiskFileDevice()->getBasePath();
	return compareStringN(base_path, normalized_path, stringLength(base_path)) == 0;
}


void ImportAssetDialog::onMaterialsGUI()
{
	StaticString<30> label("Materials (");
	label << m_fbx_importer->materials.size() << ")###Materials";
	if (!ImGui::CollapsingHeader(label)) return;

	ImGui::Indent();
	if (ImGui::Button("Import all materials"))
	{
		for (auto& mat : m_fbx_importer->materials) mat.import = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Do not import any materials"))
	{
		for (auto& mat : m_fbx_importer->materials) mat.import = false;
	}
	if (ImGui::Button("Import all textures"))
	{
		for (auto& mat : m_fbx_importer->materials)
		{
			for (auto& tex : mat.textures) tex.import = true;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Do not import any textures"))
	{
		for (auto& mat : m_fbx_importer->materials)
		{
			for (auto& tex : mat.textures) tex.import = false;
		}
	}
	for (auto& mat : m_fbx_importer->materials)
	{
		const char* material_name = mat.fbx->name;
		if (ImGui::TreeNode(mat.fbx, "%s", material_name))
		{
			ImGui::Checkbox("Import material", &mat.import);
			ImGui::Checkbox("Alpha cutout material", &mat.alpha_cutout);

			ImGui::Columns(4);
			ImGui::Text("Path");
			ImGui::NextColumn();
			ImGui::Text("Import");
			ImGui::NextColumn();
			ImGui::Text("Convert to DDS");
			ImGui::NextColumn();
			ImGui::Text("Source");
			ImGui::NextColumn();
			ImGui::Separator();
			for (int i = 0; i < lengthOf(mat.textures); ++i)
			{
				if (!mat.textures[i].fbx) continue;
				ImGui::Text("%s", mat.textures[i].path.data);
				ImGui::NextColumn();
				ImGui::Checkbox(StaticString<20>("###imp", i), &mat.textures[i].import);
				ImGui::NextColumn();
				ImGui::Checkbox(StaticString<20>("###dds", i), &mat.textures[i].to_dds);
				ImGui::NextColumn();
				if (ImGui::Button(StaticString<50>("Browse###brw", i)))
				{
					if (PlatformInterface::getOpenFilename(
							mat.textures[i].src.data, lengthOf(mat.textures[i].src.data), "All\0*.*\0", nullptr))
					{
						mat.textures[i].is_valid = true;
					}
				}
				ImGui::SameLine();
				ImGui::Text("%s", mat.textures[i].src.data);
				ImGui::NextColumn();
			}
			ImGui::Columns();

			ImGui::TreePop();
		}
	}
	ImGui::Unindent();
}


void ImportAssetDialog::onLODsGUI()
{
	if (!ImGui::CollapsingHeader("LODs")) return;
	for (int i = 0; i < lengthOf(m_fbx_importer->lods_distances); ++i)
	{
		bool b = m_fbx_importer->lods_distances[i] < 0;
		if (ImGui::Checkbox(StaticString<20>("Infinite###lod_inf", i), &b))
		{
			m_fbx_importer->lods_distances[i] *= -1;
		}
		if (m_fbx_importer->lods_distances[i] >= 0)
		{
			ImGui::SameLine();
			ImGui::DragFloat(StaticString<10>("LOD ", i), &m_fbx_importer->lods_distances[i], 1.0f, 1.0f, FLT_MAX);
		}
	}
}


void ImportAssetDialog::onAnimationsGUI()
{
	StaticString<30> label("Animations (");
	label << m_fbx_importer->animations.size() << ")###Animations";
	if (!ImGui::CollapsingHeader(label)) return;

	ImGui::DragFloat("Time scale", &m_fbx_importer->time_scale, 1.0f, 0, FLT_MAX, "%.5f");
	ImGui::DragFloat("Max position error", &m_fbx_importer->position_error, 0, FLT_MAX);
	ImGui::DragFloat("Max rotation error", &m_fbx_importer->rotation_error, 0, FLT_MAX);

	ImGui::Indent();
	ImGui::Columns(4);
	
	ImGui::Text("Name");
	ImGui::NextColumn();
	ImGui::Text("Import");
	ImGui::NextColumn();
	ImGui::Text("Root motion bone");
	ImGui::NextColumn();
	ImGui::Text("Splits");
	ImGui::NextColumn();
	ImGui::Separator();

	ImGui::PushID("anims");
	for (int i = 0; i < m_fbx_importer->animations.size(); ++i)
	{
		FBXImporter::ImportAnimation& animation = m_fbx_importer->animations[i];
		ImGui::PushID(i);
		ImGui::InputText("###name", animation.output_filename.data, lengthOf(animation.output_filename.data));
		ImGui::NextColumn();
		ImGui::Checkbox("", &animation.import);
		ImGui::NextColumn();
		auto getter = [](void* data, int idx, const char** out) -> bool {
			auto* that = (ImportAssetDialog*)data;
			*out = that->m_fbx_importer->bones[idx]->name;
			return true;
		};
		ImGui::Combo("##rb", &animation.root_motion_bone_idx, getter, this, m_fbx_importer->bones.size());
		ImGui::NextColumn();
		if (ImGui::Button("Add split")) animation.splits.emplace();
		for (int i = 0; i < animation.splits.size(); ++i)
		{
			auto& split = animation.splits[i];
			if (ImGui::TreeNodeEx(StaticString<64>("", i)))
			{
				ImGui::InputText("Name", split.name.data, sizeof(split.name.data));
				ImGui::InputInt("From", &split.from_frame);
				ImGui::InputInt("To", &split.to_frame);
				if (ImGui::Button("Remove"))
				{
					animation.splits.erase(i);
					--i;
				}
				ImGui::TreePop();
			}
		}
		ImGui::NextColumn();
		ImGui::PopID();
	}

	ImGui::PopID();
	ImGui::Columns();
	ImGui::Unindent();
}


void ImportAssetDialog::onMeshesGUI()
{
	StaticString<30> label("Meshes (");
	label << m_fbx_importer->meshes.size() << ")###Meshes";
	if (!ImGui::CollapsingHeader(label)) return;

	ImGui::InputText("Output mesh filename", m_mesh_output_filename, sizeof(m_mesh_output_filename));

	ImGui::Indent();
	ImGui::Columns(5);

	ImGui::Text("Mesh");
	ImGui::NextColumn();
	ImGui::Text("Material");
	ImGui::NextColumn();
	ImGui::Text("Import mesh");
	ImGui::NextColumn();
	ImGui::Text("Import physics");
	ImGui::NextColumn();
	ImGui::Text("LOD");
	ImGui::NextColumn();
	ImGui::Separator();

	for (auto& mesh : m_fbx_importer->meshes)
	{
		const char* name = mesh.fbx->name;
		ImGui::Text("%s", name);
		ImGui::NextColumn();

		auto* material = mesh.fbx_mat;
		ImGui::Text("%s", material->name);
		ImGui::NextColumn();

		ImGui::Checkbox(StaticString<30>("###mesh", (u64)&mesh), &mesh.import);
		if (ImGui::GetIO().MouseClicked[1] && ImGui::IsItemHovered()) ImGui::OpenPopup("ContextMesh");
		ImGui::NextColumn();
		ImGui::Checkbox(StaticString<30>("###phy", (u64)&mesh), &mesh.import_physics);
		if (ImGui::GetIO().MouseClicked[1] && ImGui::IsItemHovered()) ImGui::OpenPopup("ContextPhy");
		ImGui::NextColumn();
		ImGui::Combo(StaticString<30>("###lod", (u64)&mesh), &mesh.lod, "LOD 1\0LOD 2\0LOD 3\0LOD 4\0");
		ImGui::NextColumn();
	}
	ImGui::Columns();
	ImGui::Unindent();
	if (ImGui::BeginPopup("ContextMesh"))
	{
		if (ImGui::Selectable("Select all"))
		{
			for (auto& mesh : m_fbx_importer->meshes) mesh.import = true;
		}
		if (ImGui::Selectable("Deselect all"))
		{
			for (auto& mesh : m_fbx_importer->meshes) mesh.import = false;
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("ContextPhy"))
	{
		if (ImGui::Selectable("Select all"))
		{
			for (auto& mesh : m_fbx_importer->meshes) mesh.import_physics = true;
		}
		if (ImGui::Selectable("Deselect all"))
		{
			for (auto& mesh : m_fbx_importer->meshes) mesh.import_physics = false;
		}
		ImGui::EndPopup();
	}
}


void ImportAssetDialog::onImageGUI()
{
	if (!isImage(m_source)) return;

	if (ImGui::Checkbox("Convert to raw", &m_convert_to_raw))
	{
		if (m_convert_to_raw) m_convert_to_dds = false;
	}
	int dxt_quality = s_default_comp_params.m_dxt_quality;
	if (ImGui::Combo("Quality", &dxt_quality, "Super Fast\0Fast\0Normal\0Better\0Uber\0"))
	{
		s_default_comp_params.m_dxt_quality = (crn_dxt_quality)dxt_quality;
	}
	
	int quality_lvl = s_default_comp_params.m_quality_level;
	if (ImGui::DragInt("Quality level", &quality_lvl, 1, cCRNMinQualityLevel, cCRNMaxQualityLevel))
	{
		s_default_comp_params.m_quality_level = quality_lvl;
	}

	int compressor_type = s_default_comp_params.m_dxt_compressor_type;
	if (ImGui::Combo("Compressor type", &compressor_type, "CRN\0CRN fast\0RYG\0"))
	{
		s_default_comp_params.m_dxt_compressor_type = (crn_dxt_compressor_type)compressor_type;
	}

	if (ImGui::Checkbox("Normal map", &m_is_normal_map))
	{
		if (m_is_normal_map) m_convert_to_dds = true;
	}
	if (m_convert_to_raw)
	{
		ImGui::SameLine();
		ImGui::DragFloat("Scale", &m_raw_texture_scale, 1.0f, 0.01f, 256.0f);
	}
	if (ImGui::Checkbox("Convert to DDS", &m_convert_to_dds))
	{
		if (m_convert_to_dds) m_convert_to_raw = false;
	}
	ImGui::InputText("Output directory", m_output_dir, sizeof(m_output_dir));
	ImGui::SameLine();
	if (ImGui::Button("...###browseoutput"))
	{
		auto* base_path = m_editor.getEngine().getDiskFileDevice()->getBasePath();
		PlatformInterface::getOpenDirectory(m_output_dir, sizeof(m_output_dir), base_path);
	}

	if (m_image.data)
	{
		ImGui::LabelText("Size", "%d x %d", m_image.width, m_image.height);

		ImGui::InputInt2("Resize", m_image.resize_size);
		if (ImGui::Button("Resize")) resizeImage(this, m_image.resize_size[0], m_image.resize_size[1]);
	}

	if (ImGui::Button("Import texture")) importTexture();
}


static void preprocessBillboardNormalmap(u32* pixels, int width, int height, IAllocator& allocator)
{
	union {
		u32 ui32;
		u8 arr[4];
	} un;
	for (int j = 0; j < height; ++j)
	{
		for (int i = 0; i < width; ++i)
		{
			un.ui32 = pixels[i + j * width];
			u8 tmp = un.arr[1];
			un.arr[1] = un.arr[2];
			un.arr[2] = tmp;
			pixels[i + j * width] = un.ui32;
		}
	}
}


static void preprocessBillboard(u32* pixels, int width, int height, IAllocator& allocator)
{
	struct DistanceFieldCell
	{
		u32 distance;
		u32 color;
	};

	Array<DistanceFieldCell> distance_field(allocator);
	distance_field.resize(width * height);

	static const u32 ALPHA_MASK = 0xff000000;
	
	for (int j = 0; j < height; ++j)
	{
		for (int i = 0; i < width; ++i)
		{
			distance_field[i + j * width].color = pixels[i + j * width];
			distance_field[i + j * width].distance = 0xffffFFFF;
		}
	}

	for (int j = 1; j < height; ++j)
	{
		for (int i = 1; i < width; ++i)
		{
			int idx = i + j * width;
			if ((pixels[idx] & ALPHA_MASK) != 0)
			{
				distance_field[idx].distance = 0;
			}
			else
			{
				if (distance_field[idx - 1].distance < distance_field[idx - width].distance)
				{
					distance_field[idx].distance = distance_field[idx - 1].distance + 1;
					distance_field[idx].color =
						(distance_field[idx - 1].color & ~ALPHA_MASK) | (distance_field[idx].color & ALPHA_MASK);
				}
				else
				{
					distance_field[idx].distance = distance_field[idx - width].distance + 1;
					distance_field[idx].color =
						(distance_field[idx - width].color & ~ALPHA_MASK) | (distance_field[idx].color & ALPHA_MASK);
				}
			}
		}
	}

	for (int j = height - 2; j >= 0; --j)
	{
		for (int i = width - 2; i >= 0; --i)
		{
			int idx = i + j * width;
			if (distance_field[idx + 1].distance < distance_field[idx + width].distance &&
				distance_field[idx + 1].distance < distance_field[idx].distance)
			{
				distance_field[idx].distance = distance_field[idx + 1].distance + 1;
				distance_field[idx].color =
					(distance_field[idx + 1].color & ~ALPHA_MASK) | (distance_field[idx].color & ALPHA_MASK);
			}
			else if (distance_field[idx + width].distance < distance_field[idx].distance)
			{
				distance_field[idx].distance = distance_field[idx + width].distance + 1;
				distance_field[idx].color =
					(distance_field[idx + width].color & ~ALPHA_MASK) | (distance_field[idx].color & ALPHA_MASK);
			}
		}
	}

	for (int j = 0; j < height; ++j)
	{
		for (int i = 0; i < width; ++i)
		{
			pixels[i + j * width] = distance_field[i + j*width].color;
		}
	}

}


static bool createBillboard(ImportAssetDialog& dialog,
	const Path& mesh_path,
	const Path& out_path,
	const Path& out_path_normal,
	int texture_size)
{
	auto& engine = dialog.getEditor().getEngine();
	auto& universe = engine.createUniverse(false);

	auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
	if (!renderer) return false;

	auto* render_scene = static_cast<RenderScene*>(universe.getScene(crc32("renderer")));
	if (!render_scene) return false;

	auto* pipeline = Pipeline::create(*renderer, Path("pipelines/billboard.lua"), "", engine.getAllocator());
	pipeline->load();

	auto mesh_entity = universe.createEntity({0, 0, 0}, {0, 0, 0, 0});
	static const auto MODEL_INSTANCE_TYPE = Reflection::getComponentType("renderable");
	auto mesh_cmp = render_scene->createComponent(MODEL_INSTANCE_TYPE, mesh_entity);
	render_scene->setModelInstancePath(mesh_cmp, mesh_path);

	auto mesh_left_entity = universe.createEntity({ 0, 0, 0 }, { Vec3(0, 1, 0), Math::PI * 0.5f });
	auto mesh_left_cmp = render_scene->createComponent(MODEL_INSTANCE_TYPE, mesh_left_entity);
	render_scene->setModelInstancePath(mesh_left_cmp, mesh_path);

	auto mesh_back_entity = universe.createEntity({ 0, 0, 0 }, { Vec3(0, 1, 0), Math::PI });
	auto mesh_back_cmp = render_scene->createComponent(MODEL_INSTANCE_TYPE, mesh_back_entity);
	render_scene->setModelInstancePath(mesh_back_cmp, mesh_path);

	auto mesh_right_entity = universe.createEntity({ 0, 0, 0 }, { Vec3(0, 1, 0), Math::PI * 1.5f});
	auto mesh_right_cmp = render_scene->createComponent(MODEL_INSTANCE_TYPE, mesh_right_entity);
	render_scene->setModelInstancePath(mesh_right_cmp, mesh_path);

	auto light_entity = universe.createEntity({0, 0, 0}, {0, 0, 0, 0});
	static const auto GLOBAL_LIGHT_TYPE = Reflection::getComponentType("global_light");
	auto light_cmp = render_scene->createComponent(GLOBAL_LIGHT_TYPE, light_entity);
	render_scene->setGlobalLightIntensity(light_cmp, 0);

	while (engine.getFileSystem().hasWork()) engine.getFileSystem().updateAsyncTransactions();

	auto* model = render_scene->getModelInstanceModel(mesh_cmp);
	int width = 640, height = 480;
	float original_lod_0 = FLT_MAX;
	if (model->isReady())
	{
		auto* lods = model->getLODs();
		original_lod_0 = lods[0].distance;
		lods[0].distance = FLT_MAX;
		AABB aabb = model->getAABB();
		Vec3 size = aabb.max - aabb.min;
		universe.setPosition(mesh_left_entity, {aabb.max.x - aabb.min.z, 0, 0});
		universe.setPosition(mesh_back_entity, {aabb.max.x + size.z + aabb.max.x, 0, 0});
		universe.setPosition(mesh_right_entity, {aabb.max.x + size.x + size.z + aabb.max.x, 0, 0});
		
		BillboardSceneData data(aabb, texture_size);
		auto camera_entity = universe.createEntity(data.position, { 0, 0, 0, 1 });
		static const auto CAMERA_TYPE = Reflection::getComponentType("camera");
		auto camera_cmp = render_scene->createComponent(CAMERA_TYPE, camera_entity);
		render_scene->setCameraOrtho(camera_cmp, true);
		render_scene->setCameraSlot(camera_cmp, "main");
		width = data.width;
		height = data.height;
		render_scene->setCameraOrthoSize(camera_cmp, data.ortho_size);
	}

	pipeline->setScene(render_scene);
	pipeline->resize(width, height);
	pipeline->render();

	bgfx::TextureHandle texture =
		bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK);
	renderer->viewCounterAdd();
	bgfx::touch(renderer->getViewCounter());
	bgfx::setViewName(renderer->getViewCounter(), "billboard_blit");
	bgfx::TextureHandle color_renderbuffer = pipeline->getRenderbuffer("g_buffer", 0);
	bgfx::blit(renderer->getViewCounter(), texture, 0, 0, color_renderbuffer);

	bgfx::TextureHandle normal_texture =
		bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK);
	renderer->viewCounterAdd();
	bgfx::touch(renderer->getViewCounter());
	bgfx::setViewName(renderer->getViewCounter(), "billboard_blit_normal");
	bgfx::TextureHandle normal_renderbuffer = pipeline->getRenderbuffer("g_buffer", 1);
	bgfx::blit(renderer->getViewCounter(), normal_texture, 0, 0, normal_renderbuffer);

	renderer->viewCounterAdd();
	bgfx::setViewName(renderer->getViewCounter(), "billboard_read");
	Array<u8> data(engine.getAllocator());
	data.resize(width * height * 4);
	bgfx::readTexture(texture, &data[0]);
	bgfx::touch(renderer->getViewCounter());

	renderer->viewCounterAdd();
	bgfx::setViewName(renderer->getViewCounter(), "billboard_read_normal");
	Array<u8> data_normal(engine.getAllocator());
	data_normal.resize(width * height * 4);
	bgfx::readTexture(normal_texture, &data_normal[0]);
	bgfx::touch(renderer->getViewCounter());

	bgfx::frame(); // submit
	bgfx::frame(); // wait for gpu

	if (model->isReady())
	{
		auto* lods = model->getLODs();
		lods[0].distance = original_lod_0;
	}

	preprocessBillboard((u32*)&data[0], width, height, engine.getAllocator());
	preprocessBillboardNormalmap((u32*)&data_normal[0], width, height, engine.getAllocator());
	saveAsDDS(dialog, "billboard_generator", (u8*)&data[0], width, height, true, false, out_path.c_str());
	saveAsDDS(dialog, "billboard_generator", (u8*)&data_normal[0], width, height, true, true, out_path_normal.c_str());
	bgfx::destroy(texture);
	bgfx::destroy(normal_texture);
	Pipeline::destroy(pipeline);
	engine.destroyUniverse(universe);
	
	return true;
}


void ImportAssetDialog::convert(bool use_ui)
{
	ASSERT(!m_task);
	
	if (m_sources.empty())
	{
		setMessage("Nothing to import.");
		return;
	}

	if (m_fbx_importer->bones.size() > Model::Bone::MAX_COUNT)
	{
		setMessage("Too many bones.");
		return;
	}

	for (auto& material : m_fbx_importer->materials)
	{
		for (auto& tex : material.textures)
		{
			if (tex.fbx && !tex.is_valid && tex.import)
			{
				if (use_ui) ImGui::OpenPopup("Invalid texture");
				else g_log_error.log("Editor") << "Invalid texture " << tex.src;
				return;
			}
		}
	}

	saveModelMetadata();

	IAllocator& allocator = m_editor.getAllocator();
	m_task = makeTask(
		[this]() {
			char output_dir[MAX_PATH_LENGTH];
			m_editor.makeAbsolute(output_dir, lengthOf(output_dir), m_output_dir);
			char tmp[MAX_PATH_LENGTH];
			char texture_output_dir[MAX_PATH_LENGTH];
			PathUtils::normalize(m_texture_output_dir, texture_output_dir, lengthOf(texture_output_dir));
			m_editor.makeRelative(tmp, lengthOf(tmp), texture_output_dir);
			copyString(texture_output_dir, tmp[0] ? "/" : "");
			catString(texture_output_dir, tmp);
			if (m_fbx_importer->save(output_dir, m_mesh_output_filename, texture_output_dir))
			{
				for (auto& mat : m_fbx_importer->materials)
				{
					for (int i = 0; i < lengthOf(mat.textures); ++i)
					{
						auto& tex = mat.textures[i];

						if (!tex.fbx) continue;
						if (!tex.import) continue;

						PathUtils::FileInfo texture_info(tex.src);
						PathBuilder dest(m_texture_output_dir[0] ? m_texture_output_dir : m_output_dir);
						dest << "/" << texture_info.m_basename << (tex.to_dds ? ".dds" : texture_info.m_extension);

						bool is_src_dds = equalStrings(texture_info.m_extension, "dds");
						if (tex.to_dds && !is_src_dds)
						{
							int image_width, image_height, image_comp;
							auto data = stbi_load(tex.src, &image_width, &image_height, &image_comp, 4);
							if (!data)
							{
								StaticString<MAX_PATH_LENGTH + 20> error_msg("Could not load image ", tex.src);
								setMessage(error_msg);
								return;
							}

							bool is_normal_map = i == FBXImporter::ImportTexture::NORMAL;
							if (!saveAsDDS(*this,
									tex.src,
									data,
									image_width,
									image_height,
									image_comp == 4,
									is_normal_map,
									dest))
							{
								stbi_image_free(data);
								setMessage(
									StaticString<MAX_PATH_LENGTH * 2 + 20>("Error converting ", tex.src, " to ", dest));
								return;
							}
							stbi_image_free(data);
						}
						else
						{
							if (equalStrings(tex.src, dest))
							{
								if (!PlatformInterface::fileExists(tex.src))
								{
									setMessage(StaticString<MAX_PATH_LENGTH + 20>(tex.src, " not found"));
									return;
								}
							}
							else if (!copyFile(tex.src, dest))
							{
								setMessage(
									StaticString<MAX_PATH_LENGTH * 2 + 20>("Error copying ", tex.src, " to ", dest));
								return;
							}
						}
					}
				}

				setMessage("Success.");
			}
		},
		[this]() {
			if (m_fbx_importer->create_billboard_lod)
			{
				PathBuilder mesh_path(m_output_dir, "/");
				mesh_path << m_mesh_output_filename << ".msh";

				if (m_texture_output_dir[0])
				{
					PathBuilder texture_path(m_texture_output_dir, m_mesh_output_filename, "_billboard.dds");
					PathBuilder normal_texture_path(
						m_texture_output_dir, m_mesh_output_filename, "_billboard_normal.dds");
					createBillboard(*this,
						Path(mesh_path),
						Path(texture_path),
						Path(normal_texture_path),
						BillboardSceneData::TEXTURE_SIZE);
				}
				else
				{
					PathBuilder texture_path(m_output_dir, "/", m_mesh_output_filename, "_billboard.dds");
					PathBuilder normal_texture_path(m_output_dir, "/", m_mesh_output_filename, "_billboard_normal.dds");
					createBillboard(*this,
						Path(mesh_path),
						Path(texture_path),
						Path(normal_texture_path),
						BillboardSceneData::TEXTURE_SIZE);
				}
			}
		},
		allocator);
	m_task->create("ConvertTask");

}


void ImportAssetDialog::import()
{
	convert(false);
}


void ImportAssetDialog::checkTask(bool wait)
{
	if (!m_task) return;
	if (!wait && !m_task->isFinished()) return;

	if (wait)
	{
		while (!m_task->isFinished()) MT::sleep(200);
	}

	m_task->destroy();
	LUMIX_DELETE(m_editor.getAllocator(), m_task);
	m_task = nullptr;
	m_is_importing_texture = false;
}


void ImportAssetDialog::onAction()
{
	m_is_open = !m_is_open;
}


void ImportAssetDialog::clearSources()
{
	checkTask(true);
	stbi_image_free(m_image.data);
	m_image.data = nullptr;
	m_fbx_importer->clearSources();
	m_mesh_output_filename[0] = '\0';
}


void ImportAssetDialog::addSource(const char* src)
{
	copyString(m_source, src);
	checkSource();
	checkTask(true);
}


void ImportAssetDialog::onWindowGUI()
{
	if (!ImGui::BeginDock("Import Asset", &m_is_open))
	{
		ImGui::EndDock();
		return;
	}

	if (m_task)
	{
		checkTask(false);
		{
			MT::SpinLock lock(m_mutex);
			ImGui::Text("%s", m_import_message);
			if (m_progress_fraction >= 0) ImGui::ProgressBar(m_progress_fraction);
		}
		ImGui::EndDock();
		return;
	}

	if (hasMessage())
	{
		char msg[1024];
		getMessage(msg, sizeof(msg));
		ImGui::Text("%s", msg);
		if (ImGui::Button("OK"))
		{
			setMessage("");
		}
		ImGui::EndDock();
		return;
	}

	if (m_is_importing_texture)
	{
		if (ImGui::Button("Cancel"))
		{
			m_dds_convert_callback.cancel_requested = true;
		}

		checkTask(false);

		{
			MT::SpinLock lock(m_mutex);
			ImGui::Text("%s", m_import_message);
			if (m_progress_fraction >= 0) ImGui::ProgressBar(m_progress_fraction);
		}
		ImGui::EndDock();
		return;
	}


	if (ImGui::Button("Add source"))
	{
		if (PlatformInterface::getOpenFilename(m_source, sizeof(m_source), "All\0*.*\0", m_source))
		{
			checkSource();
		}
	}
	if (!m_fbx_importer->scenes.empty())
	{
		ImGui::SameLine();
		if (ImGui::Button("Clear all sources")) clearSources();
	}

	onImageGUI();

	if (!m_fbx_importer->scenes.empty())
	{
		if (ImGui::CollapsingHeader("Advanced"))
		{
			ImGui::Checkbox("Create billboard LOD", &m_fbx_importer->create_billboard_lod);
			ImGui::Checkbox("Center meshes", &m_fbx_importer->center_mesh);
			ImGui::Checkbox("Import Vertex Colors", &m_fbx_importer->import_vertex_colors);
			ImGui::DragFloat("Scale", &m_fbx_importer->mesh_scale, 0.01f, 0.001f, 0);
			ImGui::Combo("Orientation", &(int&)m_fbx_importer->orientation, "Y up\0Z up\0-Z up\0-X up\0X up\0");
			ImGui::Combo("Root Orientation", &(int&)m_fbx_importer->root_orientation, "Y up\0Z up\0-Z up\0-X up\0X up\0");
			ImGui::Checkbox("Make physics convex", &m_fbx_importer->make_convex);
		}

		onMeshesGUI();
		onLODsGUI();
		onMaterialsGUI();
		onAnimationsGUI();

		if (ImGui::CollapsingHeader("Output", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputText("Output directory", m_output_dir, sizeof(m_output_dir));
			ImGui::SameLine();
			if (ImGui::Button("...###browseoutput"))
			{
				if (PlatformInterface::getOpenDirectory(m_output_dir, sizeof(m_output_dir), m_last_dir))
				{
					copyString(m_last_dir, m_output_dir);
				}
			}

			ImGui::InputText("Texture output directory", m_texture_output_dir, sizeof(m_texture_output_dir));
			ImGui::SameLine();
			if (ImGui::Button("...###browsetextureoutput"))
			{
				if (PlatformInterface::getOpenDirectory(m_texture_output_dir, sizeof(m_texture_output_dir), m_last_dir))
				{
					copyString(m_last_dir, m_texture_output_dir);
				}
			}

			if (m_output_dir[0] != '\0')
			{
				if (!isTextureDirValid())
				{
					ImGui::Text("Texture output directory must be an ancestor of the working "
						"directory or empty.");
				}
				else if (ImGui::Button("Convert"))
				{
					convert(true);
				}
			}
		}


		if (ImGui::BeginPopupModal("Invalid texture"))
		{
			for (auto& mat : m_fbx_importer->materials)
			{
				for (auto& tex : mat.textures)
				{
					if (!tex.fbx || tex.is_valid || !tex.import) continue;
					ImGui::Text("Texture %s is not valid", tex.path.data);
				}
			}
			if (ImGui::Button("OK"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
	ImGui::EndDock();
}


} // namespace Lumix