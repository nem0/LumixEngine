#include "import_asset_dialog.h"
#include "animation/animation.h"
#include "assimp/DefaultLogger.hpp"
#include "assimp/ProgressHandler.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "crnlib.h"
#include "editor/metadata.h"
#include "editor/platform_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/fs/disk_file_device.h"
#include "engine/fs/os_file.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math_utils.h"
#include "engine/mt/task.h"
#include "engine/mt/thread.h"
#include "engine/path_utils.h"
#include "engine/property_register.h"
#include "engine/system.h"
#include "engine/debug/floating_points.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "physics/physics_geometry_manager.h"
#include "renderer/frame_buffer.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "stb/stb_image.h"


using namespace Lumix;


typedef StaticString<MAX_PATH_LENGTH> PathBuilder;


enum class VertexAttributeDef : uint32
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


#pragma pack(1)
struct BillboardVertex
{
	Vec3 pos;
	uint8 normal[4];
	uint8 tangent[4];
	Vec2 uv;
};
#pragma pack()


static const int TEXTURE_SIZE = 512;


static bool isSkinned(const aiMesh* mesh) { return mesh->mNumBones > 0; }


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


static bool isSkinned(const aiScene* scene, const aiMaterial* material)
{
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
	{
		if (scene->mMaterials[scene->mMeshes[i]->mMaterialIndex] == material && isSkinned(scene->mMeshes[i]))
		{
			return true;
		}
	}
	return false;
}


static const aiNode* getOwner(const aiNode* node, int mesh_index)
{
	for (unsigned int i = 0; i < (int)node->mNumMeshes; ++i)
	{
		if (node->mMeshes[i] == mesh_index) return node;
	}

	for (int i = 0; i < (int)node->mNumChildren; ++i)
	{
		auto* child = node->mChildren[i];
		auto* owner = getOwner(child, mesh_index);
		if (owner) return owner;
	}

	return nullptr;
}


static const aiNode* getOwner(const aiScene* scene, const aiMesh* mesh)
{
	for (int i = 0; i < int(scene->mNumMeshes); ++i)
	{
		if (scene->mMeshes[i] == mesh) return getOwner(scene->mRootNode, i);
	}
	return nullptr;
}


static aiString getMeshName(const aiScene* scene, const aiMesh* mesh)
{
	aiString mesh_name = mesh->mName;
	int length = stringLength(mesh_name.C_Str());
	if (length == 0)
	{
		const auto* node = getOwner(scene, mesh);
		if (node)
		{
			mesh_name = node->mName;
		}
	}
	return mesh_name;
}


static float getMeshLODFactor(const aiScene* scene, const aiMesh* mesh)
{
	const char* mesh_name = getMeshName(scene, mesh).C_Str();
	int len = stringLength(mesh_name);
	if (len < 5) return FLT_MAX;

	const char* last = mesh_name + len - 1;
	while (last > mesh_name && *last >= '0' && *last <= '9')
	{
		--last;
	}
	++last;
	if (last < mesh_name + 4) return FLT_MAX;
	if (compareIStringN(last - 4, "_LOD", 4) != 0) return FLT_MAX;
	const char* end_of_factor = last - 4;
	const char* begin_factor = end_of_factor - 1;
	if (begin_factor <= mesh_name) return FLT_MAX;

	while (*begin_factor != '_' && begin_factor > mesh_name)
	{
		--begin_factor;
	}
	++begin_factor;

	if (begin_factor == end_of_factor) return FLT_MAX;
	int factor;
	fromCString(begin_factor, int(end_of_factor - begin_factor), &factor);

	return float(factor);
}


static int importAsset(lua_State* L)
{
	auto* dlg = LuaWrapper::checkArg<ImportAssetDialog*>(L, 1);
	return dlg->importAsset(L);
}


static int getMeshLOD(const aiScene* scene, const aiMesh* mesh)
{
	const char* mesh_name = getMeshName(scene, mesh).C_Str();
	int len = stringLength(mesh_name);
	if (len < 5) return 0;

	const char* last = mesh_name + len - 1;
	while (last > mesh_name && *last >= '0' && *last <= '9')
	{
		--last;
	}
	++last;
	if (last < mesh_name + 4) return 0;
	if (compareIStringN(last - 4, "_LOD", 4) != 0) return 0;

	int lod;
	fromCString(last, len - int(last - mesh_name), &lod);

	return lod;
}


static bool hasSimilarFace(const aiMesh& mesh, Array<aiFace*>& faces, const aiFace& face)
{
	static const float MAX_ERROR = 0.001f;
	auto isSame = [](const aiVector3D& a, const aiVector3D& b) {
		return fabs(a.x - b.x) < MAX_ERROR && fabs(a.y - b.y) < MAX_ERROR && fabs(a.z - b.z) < MAX_ERROR;
	};
	auto f0 = mesh.mVertices[face.mIndices[0]];
	auto f1 = mesh.mVertices[face.mIndices[1]];
	auto f2 = mesh.mVertices[face.mIndices[2]];
	for (auto* tmp : faces)
	{
		auto v0 = mesh.mVertices[tmp->mIndices[0]];
		auto v1 = mesh.mVertices[tmp->mIndices[1]];
		auto v2 = mesh.mVertices[tmp->mIndices[2]];
		if (fabs(v0.x - f0.x) < MAX_ERROR || fabs(v1.x - f0.x) < MAX_ERROR || fabs(v2.x - f0.x) < MAX_ERROR)
		{
			if (isSame(v0, f0))
			{
				if (isSame(v1, f1) && isSame(v2, f2)) return true;
				if (isSame(v1, f2) && isSame(v2, f1)) return true;
			}
			if (isSame(v0, f1))
			{
				if (isSame(v1, f2) && isSame(v2, f0)) return true;
				if (isSame(v1, f0) && isSame(v2, f2)) return true;
			}
			if (isSame(v0, f2))
			{
				if (isSame(v1, f1) && isSame(v2, f0)) return true;
				if (isSame(v1, f0) && isSame(v2, f1)) return true;
			}
		}
	}
	return false;
}


enum class Preprocesses
{
	REMOVE_DOUBLES = 1
};


static void preprocessMesh(ImportMesh& mesh, uint32 flags, IAllocator& allocator)
{
	Array<aiFace*> faces(allocator);
	mesh.map_from_input.clear();
	mesh.map_to_input.clear();
	mesh.indices.clear();

	bool remove_doubles = (flags & (uint32)Preprocesses::REMOVE_DOUBLES) != 0;
	for (unsigned int f = 0; f < mesh.mesh->mNumFaces; ++f)
	{
		auto& face = mesh.mesh->mFaces[f];
		ASSERT(face.mNumIndices == 3);
		if (!remove_doubles || !hasSimilarFace(*mesh.mesh, faces, face)) faces.push(&face);
	}

	mesh.map_to_input.reserve(faces.size() * 3);
	mesh.map_from_input.resize(mesh.mesh->mNumFaces * 3);
	for (unsigned int& i : mesh.map_from_input) i = 0xffffFFFF;

	for (auto& face : faces)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (mesh.map_from_input[face->mIndices[i]] == 0xffffFFFF)
			{
				mesh.map_to_input.push(face->mIndices[i]);
				mesh.map_from_input[face->mIndices[i]] = mesh.map_to_input.size() - 1;
			}
		}
	}

	mesh.indices.reserve(faces.size() * 3);
	for (auto& face : faces)
	{
		for (int i = 0; i < 3; ++i)
		{
			mesh.indices.push(mesh.map_from_input[face->mIndices[i]]);
		}
	}
}


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
		const char* base_path = editor.getEngine().getPatchFileDevice()->getBasePath();
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
	const uint8* image_data,
	int image_width,
	int image_height,
	const char* dest_path,
	float scale,
	IAllocator& allocator)
{
	ASSERT(image_data);

	dialog.setImportMessage(StaticString<MAX_PATH_LENGTH + 30>("Saving ") << dest_path, -1);

	FS::OsFile file;
	if (!file.open(dest_path, FS::Mode::CREATE_AND_WRITE, dialog.getEditor().getAllocator()))
	{
		dialog.setMessage(StaticString<MAX_PATH_LENGTH + 30>("Could not save ") << dest_path);
		return false;
	}

	Array<uint16> data(allocator);
	data.resize(image_width * image_height);
	for (int j = 0; j < image_height; ++j)
	{
		for (int i = 0; i < image_width; ++i)
		{
			data[i + j * image_width] = uint16(scale * image_data[(i + j * image_width) * 4]);
		}
	}

	file.write((const char*)&data[0], data.size() * sizeof(data[0]));
	file.close();
	return true;
}


static bool saveAsDDS(ImportAssetDialog& dialog,
	const char* source_path,
	const uint8* image_data,
	int image_width,
	int image_height,
	bool alpha,
	const char* dest_path)
{
	ASSERT(image_data);

	dialog.setImportMessage(StaticString<MAX_PATH_LENGTH + 30>("Saving ") << dest_path, 0);

	dialog.getDDSConvertCallbackData().dialog = &dialog;
	dialog.getDDSConvertCallbackData().dest_path = dest_path;
	dialog.getDDSConvertCallbackData().cancel_requested = false;

	crn_uint32 size;
	crn_comp_params comp_params;
	comp_params.m_width = image_width;
	comp_params.m_height = image_height;
	comp_params.m_file_type = cCRNFileTypeDDS;
	comp_params.m_format = alpha ? cCRNFmtDXT5 : cCRNFmtDXT1;
	comp_params.m_quality_level = cCRNMinQualityLevel;
	comp_params.m_dxt_quality = cCRNDXTQualitySuperFast;
	comp_params.m_dxt_compressor_type = cCRNDXTCompressorRYG;
	comp_params.m_pProgress_func = ddsConvertCallback;
	comp_params.m_pProgress_func_data = &dialog.getDDSConvertCallbackData();
	comp_params.m_num_helper_threads = 3;
	comp_params.m_pImages[0][0] = (uint32*)image_data;
	crn_mipmap_params mipmap_params;
	mipmap_params.m_mode = cCRNMipModeGenerateMips;

	void* data = crn_compress(comp_params, mipmap_params, size);
	if (!data)
	{
		dialog.setMessage(StaticString<MAX_PATH_LENGTH + 30>("Could not convert ") << source_path);
		return false;
	}

	FS::OsFile file;
	if (!file.open(dest_path, FS::Mode::CREATE_AND_WRITE, dialog.getEditor().getAllocator()))
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


struct ImportTextureTask : public MT::Task
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
		int image_width;
		int image_height;
		int image_comp;
		auto* data = stbi_load(m_dialog.m_source, &image_width, &image_height, &image_comp, 4);

		if (!data)
		{
			m_dialog.setMessage(
				StaticString<MAX_PATH_LENGTH + 200>("Could not load ") << m_dialog.m_source << " : "
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
				data,
				image_width,
				image_height,
				image_comp == 4,
				dest_path);
		}
		else if (m_dialog.m_convert_to_raw)
		{
			m_dialog.setImportMessage("Converting to RAW...", -1);

			saveAsRaw(m_dialog,
				m_dialog.m_editor.getEngine().getFileSystem(),
				data,
				image_width,
				image_height,
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
		stbi_image_free(data);

		return 0;
	}


	ImportAssetDialog& m_dialog;

}; // struct ImportTextureTask


struct ImportTask : public MT::Task
{
	struct ProgressHandler : public Assimp::ProgressHandler
	{
		bool Update(float percentage) override
		{
			task->m_dialog.setImportMessage(StaticString<50>("Importing... "), percentage);

			return !cancel_requested;
		}

		ImportTask* task;
		bool cancel_requested;
	};

	explicit ImportTask(ImportAssetDialog& dialog)
		: Task(dialog.m_editor.getAllocator())
		, m_dialog(dialog)
	{
		m_dialog.m_importers.back().SetProgressHandler(&m_progress_handler);
		struct MyStream : public Assimp::LogStream
		{
			void write(const char* message) { g_log_warning.log("Editor") << message; }
		};
		const unsigned int severity = Assimp::Logger::Err;
		Assimp::DefaultLogger::create(ASSIMP_DEFAULT_LOG_NAME, Assimp::Logger::NORMAL, 0, nullptr);

		Assimp::DefaultLogger::get()->attachStream(new MyStream(), severity);
	}


	~ImportTask() { m_dialog.m_importers.back().SetProgressHandler(nullptr); }


	bool isValidFilenameChar(char c)
	{
		if (c >= 'A' && c <= 'Z') return true;
		if (c >= 'a' && c <= 'z') return true;
		if (c >= '0' && c <= '9') return true;
		return false;
	}


	int task() override
	{
		m_progress_handler.task = this;
		m_progress_handler.cancel_requested = false;
		enableFloatingPointTraps(false);
		
		auto& importer = m_dialog.m_importers.back();
		importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_LIGHTS | aiComponent_CAMERAS);

		unsigned int flags = aiProcess_JoinIdenticalVertices | aiProcess_RemoveComponent | aiProcess_GenUVCoords |
							 aiProcess_RemoveRedundantMaterials | aiProcess_Triangulate | aiProcess_FindInvalidData |
							 aiProcess_OptimizeGraph | aiProcess_ValidateDataStructure | aiProcess_CalcTangentSpace;
		flags |= m_dialog.m_model.gen_smooth_normal ? aiProcess_GenSmoothNormals : aiProcess_GenNormals;
		flags |= m_dialog.m_model.optimize_mesh_on_import ? aiProcess_OptimizeMeshes : 0;

		const aiScene* scene = importer.ReadFile(m_dialog.m_source, flags);
		if (!scene)
		{
			importer.FreeScene();
			const char* msg = importer.GetErrorString();
			m_dialog.setMessage(msg);
			g_log_error.log("Editor") << msg;
			m_dialog.m_importers.pop();
		}
		else
		{
			unsigned int new_mesh_count = scene->mNumMeshes + m_dialog.m_meshes.size();
			m_dialog.m_meshes.reserve(Math::maximum(new_mesh_count, 100U));

			char src_dir[MAX_PATH_LENGTH];
			PathUtils::getDir(src_dir, lengthOf(src_dir), m_dialog.m_source);
			for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
			{
				auto& material = m_dialog.m_materials.emplace();
				material.scene = scene;
				material.import = true;
				material.alpha_cutout = false;
				material.material = scene->mMaterials[i];
				material.texture_count = 0;
				copyString(material.shader, isSkinned(scene, scene->mMaterials[i]) ? "skinned" : "rigid");
				auto types = {aiTextureType_DIFFUSE, aiTextureType_NORMALS, aiTextureType_HEIGHT};
				for (auto type : types)
				{
					for (unsigned int j = 0; j < material.material->GetTextureCount(type); ++j)
					{
						aiString texture_path;
						material.material->GetTexture(type, j, &texture_path);
						auto& texture = material.textures[material.texture_count];
						copyString(texture.path, texture_path.C_Str()[0] ? texture_path.C_Str() : "");
						copyString(texture.src, src_dir);
						if (texture_path.C_Str()[0]) catString(texture.src, texture_path.C_Str());
						texture.import = true;
						texture.to_dds = true;
						texture.is_valid = PlatformInterface::fileExists(texture.src);
						++material.texture_count;
					}
				}
			}
			for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
			{
				auto& mesh = m_dialog.m_meshes.emplace(m_dialog.m_editor.getAllocator());
				mesh.scene = scene;
				mesh.import = true;
				mesh.import_physics = false;
				mesh.mesh = scene->mMeshes[i];
				mesh.lod = getMeshLOD(scene, mesh.mesh);
				float f = getMeshLODFactor(scene, mesh.mesh);
				if (f < FLT_MAX) m_dialog.m_model.lods[mesh.lod] = f;
			}
			for (unsigned int j = 0; j < scene->mNumAnimations; ++j)
			{
				auto& animation = m_dialog.m_animations.emplace();
				animation.animation = scene->mAnimations[j];
				animation.scene = scene;
				animation.import = true;

				PathBuilder path;
				Lumix::PathUtils::getBasename(path.data, Lumix::lengthOf(path.data), m_dialog.m_source);
				for (int i = 0; i < m_dialog.m_animations.size() - 1; ++i)
				{
					if (equalStrings(path.data, m_dialog.m_animations[i].output_filename))
					{
						if (animation.animation->mName.length > 0)
						{
							char tmp[MAX_PATH_LENGTH];
							copyString(tmp, animation.animation->mName.C_Str());
							char* c = tmp;
							while (*c)
							{
								if (!isValidFilenameChar(*c)) *c = '_';
								++c;
							}
							path << tmp;
						}
						break;
					}
				}
				for (int i = 0; i < m_dialog.m_animations.size() - 1; ++i)
				{
					if (equalStrings(path.data, m_dialog.m_animations[i].output_filename))
					{
						path << j;
						i = -1;
						continue;
					}
				}

				copyString(animation.output_filename, path.data);
			}
		}

		for (int i = 1; i < lengthOf(m_dialog.m_model.lods); ++i)
		{
			if (m_dialog.m_model.lods[i - 1] < 0) m_dialog.m_model.lods[i] = -1;
		}


		enableFloatingPointTraps(true);

		return 0;
	}


	ImportAssetDialog& m_dialog;
	ProgressHandler m_progress_handler;

}; // struct ImportTask


struct ConvertTask : public MT::Task
{
	struct SkinInfo
	{
		SkinInfo()
		{
			memset(this, 0, sizeof(SkinInfo));
			index = 0;
		}

		float weights[4];
		uint16 bone_indices[4];
		int index;
	};


	ConvertTask(ImportAssetDialog& dialog, float scale)
		: Task(dialog.m_editor.getAllocator())
		, m_dialog(dialog)
		, m_scale(scale)
		, m_nodes(dialog.m_editor.getAllocator())
	{
	}


	bool saveTexture(ImportTexture& texture,
		const char* source_mesh_dir,
		FS::OsFile& material_file,
		bool is_srgb) const
	{
		PathUtils::FileInfo texture_info(texture.src);
		material_file << "\t, \"texture\" : {\n\t\t\"source\" : \"";
		if (m_dialog.m_texture_output_dir[0])
		{
			char from_root_path[MAX_PATH_LENGTH];
			getRelativePath(
				m_dialog.m_editor, from_root_path, lengthOf(from_root_path), m_dialog.m_texture_output_dir);
			material_file << "/" << from_root_path;
		}
		material_file << texture_info.m_basename << ".";
		material_file << (texture.to_dds ? "dds" : texture_info.m_extension);
		material_file << (is_srgb ? "\", \"srgb\" : true\n }\n" : "\"\n }\n");

		if (!texture.import) return true;
		bool is_already_saved = m_dialog.m_saved_textures.indexOf(crc32(texture.src)) >= 0;
		if (is_already_saved) return true;

		bool is_src_dds = equalStrings(texture_info.m_extension, "dds");
		PathBuilder dest(m_dialog.m_texture_output_dir[0] ? m_dialog.m_texture_output_dir : m_dialog.m_output_dir);
		dest << "/" << texture_info.m_basename << (texture.to_dds ? ".dds" : texture_info.m_extension);
		if (texture.to_dds && !is_src_dds)
		{
			int image_width, image_height, image_comp;
			auto data = stbi_load(texture.src, &image_width, &image_height, &image_comp, 4);
			if (!data)
			{
				StaticString<MAX_PATH_LENGTH + 20> error_msg("Could not load image ", texture.src);
				m_dialog.setMessage(error_msg);
				return false;
			}

			if (!saveAsDDS(m_dialog,
					texture.src,
					data,
					image_width,
					image_height,
					image_comp == 4,
					dest))
			{
				stbi_image_free(data);
				m_dialog.setMessage(StaticString<MAX_PATH_LENGTH * 2 + 20>(
					"Error converting ", texture.src, " to ", dest));
				return false;
			}
			stbi_image_free(data);
		}
		else
		{
			if (equalStrings(texture.src, dest))
			{
				if (!PlatformInterface::fileExists(texture.src))
				{
					m_dialog.setMessage(StaticString<MAX_PATH_LENGTH + 20>(texture.src, " not found"));
					return false;
				}
			}
			else if (!copyFile(texture.src, dest))
			{
				m_dialog.setMessage(
					StaticString<MAX_PATH_LENGTH * 2 + 20>("Error copying ", texture.src, " to ", dest));
				return false;
			}
		}

		m_dialog.m_saved_textures.push(crc32(texture.src));
		return true;
	}


	Vec3 getPosition(const aiNodeAnim* channel, int frame_idx, uint32_t fps)
	{
		float time = frame_idx / (float)fps;
		unsigned int i = 0;
		while (i + 1 < channel->mNumPositionKeys && time > (float)channel->mPositionKeys[i + 1].mTime)
		{
			++i;
		}
		auto first = channel->mPositionKeys[i].mValue;

		if (i + 1 == channel->mNumPositionKeys)
		{
			return Vec3(first.x, first.y, first.z);
		}
		auto second = channel->mPositionKeys[i + 1].mValue;
		float t = float((time - channel->mPositionKeys[i].mTime) /
						(channel->mPositionKeys[i + 1].mTime - channel->mPositionKeys[i].mTime));
		first *= 1 - t;
		second *= t;

		first += second;

		return Vec3(first.x, first.y, first.z);
	}


	Quat getRotation(const aiNodeAnim* channel, int frame_idx, uint32_t fps)
	{
		float time = frame_idx / (float)fps;
		unsigned int i = 0;
		while (i + 1 < channel->mNumRotationKeys && time > (float)channel->mRotationKeys[i + 1].mTime)
		{
			++i;
		}
		auto first = channel->mRotationKeys[i].mValue;

		if (i + 1 == channel->mNumRotationKeys)
		{
			return Quat(first.x, first.y, first.z, first.w);
		}

		auto second = channel->mRotationKeys[i + 1].mValue;
		float t = float((time - channel->mRotationKeys[i].mTime) /
						(channel->mRotationKeys[i + 1].mTime - channel->mRotationKeys[i].mTime));
		aiQuaternion out;
		aiQuaternion::Interpolate(out, first, second, t);

		return Quat(out.x, out.y, out.z, out.w);
	}


	static float getLength(aiAnimation* animation)
	{
		float length = 0;
		for (unsigned int i = 0; i < animation->mNumChannels; ++i)
		{
			auto* channel = animation->mChannels[i];
			for (unsigned int j = 0; j < channel->mNumPositionKeys; ++j)
			{
				length = Math::maximum(length, (float)channel->mPositionKeys[j].mTime);
			}
			for (unsigned int j = 0; j < channel->mNumRotationKeys; ++j)
			{
				length = Math::maximum(length, (float)channel->mRotationKeys[j].mTime);
			}
			for (unsigned int j = 0; j < channel->mNumScalingKeys; ++j)
			{
				length = Math::maximum(length, (float)channel->mScalingKeys[j].mTime);
			}
		}
		return length;
	}


	bool saveLumixAnimations()
	{
		m_dialog.setImportMessage("Importing animations...", 0);
		
		bool failed = false;
		int animation_index = 0;
		int num_animations = 0;
		for (auto& import_animation : m_dialog.m_animations)
		{
			if (import_animation.import) ++num_animations;
		}

		for(auto& import_animation : m_dialog.m_animations)
		{
			if (!import_animation.import) continue;
			++animation_index;
			const aiScene* scene = import_animation.scene;
			m_dialog.setImportMessage("Importing animations...", (float)animation_index / num_animations);
			auto* animation = import_animation.animation;

			FS::OsFile file;
			PathBuilder ani_path(m_dialog.m_output_dir, "/", import_animation.output_filename, ".ani");

			if (!file.open(ani_path, FS::Mode::CREATE_AND_WRITE, m_dialog.m_editor.getAllocator()))
			{
				g_log_error.log("Editor") << "Could not create file " << ani_path;
				failed = true;
				continue;
			}

			Animation::Header header;
			header.fps = uint32(animation->mTicksPerSecond == 0
											? 25
											: (animation->mTicksPerSecond == 1 ? 30 : animation->mTicksPerSecond));
			header.magic = Animation::HEADER_MAGIC;
			header.version = 1;

			file.write(&header, sizeof(header));
			float anim_length = getLength(animation);
			int frame_count = Math::maximum(int(anim_length * header.fps), 1);
			file.write(&frame_count, sizeof(frame_count));
			int bone_count = (int)animation->mNumChannels;
			file.write(&bone_count, sizeof(bone_count));

			Array<Vec3> positions(m_dialog.m_editor.getAllocator());
			Array<Quat> rotations(m_dialog.m_editor.getAllocator());

			positions.resize(bone_count * frame_count);
			rotations.resize(bone_count * frame_count);

			for (unsigned int channel_idx = 0; channel_idx < animation->mNumChannels; ++channel_idx)
			{
				const aiNodeAnim* channel = animation->mChannels[channel_idx];
				auto global_transform = getGlobalTransform(getNode(channel->mNodeName, scene->mRootNode)->mParent);
				aiVector3t<float> scale;
				aiVector3t<float> dummy_pos;
				aiQuaterniont<float> dummy_rot;
				global_transform.Decompose(scale, dummy_rot, dummy_pos);
				for (int frame = 0; frame < frame_count; ++frame)
				{
					auto pos = getPosition(channel, frame, header.fps) * m_dialog.m_model.mesh_scale;
					pos.x *= scale.x;
					pos.y *= scale.y;
					pos.z *= scale.z;
					positions[frame * bone_count + channel_idx] = pos;
					rotations[frame * bone_count + channel_idx] = getRotation(channel, frame, header.fps);
				}
			}

			file.write((const char*)&positions[0], sizeof(positions[0]) * positions.size());
			file.write((const char*)&rotations[0], sizeof(rotations[0]) * rotations.size());
			for (unsigned int channel_idx = 0; channel_idx < animation->mNumChannels; ++channel_idx)
			{
				const aiNodeAnim* channel = animation->mChannels[channel_idx];
				uint32_t hash = crc32(channel->mNodeName.C_Str());
				file.write((const char*)&hash, sizeof(hash));
			}

			file.close();
		}

		return !failed;
	}


	bool saveLumixMaterials()
	{
		m_dialog.m_saved_textures.clear();

		int undefined_count = 0;
		char source_mesh_dir[MAX_PATH_LENGTH];
		PathUtils::getDir(source_mesh_dir, sizeof(source_mesh_dir), m_dialog.m_source);

		for (auto& material : m_dialog.m_materials)
		{
			if (!material.import) continue;
			if (!saveMaterial(material, source_mesh_dir, &undefined_count)) return false;
		}

		if (m_dialog.m_model.create_billboard_lod)
		{
			FS::OsFile file;
			PathBuilder output_material_name(m_dialog.m_output_dir, "/", m_dialog.m_mesh_output_filename, "_billboard.mat");
			if (!file.open(output_material_name, FS::Mode::CREATE_AND_WRITE, m_dialog.m_editor.getAllocator()))
			{
				m_dialog.setMessage(
					StaticString<20 + MAX_PATH_LENGTH>("Could not create ", output_material_name));
				return false;
			}
			file << "{\n\t\"shader\" : \"shaders/billboard.shd\"\n";
			file << "\t, \"defines\" : [\"ALPHA_CUTOUT\"]\n";
			file << "\t, \"texture\" : {\n\t\t\"source\" : \"";

			if (m_dialog.m_texture_output_dir[0])
			{
				char from_root_path[MAX_PATH_LENGTH];
				getRelativePath(
					m_dialog.m_editor, from_root_path, lengthOf(from_root_path), m_dialog.m_texture_output_dir);
				PathBuilder relative_texture_path(from_root_path, m_dialog.m_mesh_output_filename, "_billboard.dds");
				PathBuilder texture_path(m_dialog.m_texture_output_dir, m_dialog.m_mesh_output_filename, "_billboard.dds");
				copyFile("models/utils/cube/default.dds", texture_path);
				file << "/" << relative_texture_path;
			}
			else
			{
				file << "billboard.dds";
				PathBuilder texture_path(m_dialog.m_output_dir, "/", m_dialog.m_mesh_output_filename, "_billboard.dds");
				copyFile("models/utils/cube/default.dds", texture_path);
			}

			file << "\"}\n}";
			file.close();
		}
		return true;
	}


	bool saveMaterial(ImportMaterial& material, const char* source_mesh_dir, int* undefined_count) const
	{
		ASSERT(undefined_count);

		aiString material_name;
		material.material->Get(AI_MATKEY_NAME, material_name);
		PathBuilder output_material_name(m_dialog.m_output_dir);
		output_material_name << "/" << material_name.C_Str() << ".mat";

		m_dialog.setImportMessage(
			StaticString<MAX_PATH_LENGTH + 30>("Converting ") << output_material_name, -1);
		FS::OsFile file;
		if (!file.open(output_material_name, FS::Mode::CREATE_AND_WRITE, m_dialog.m_editor.getAllocator()))
		{
			m_dialog.setMessage(
				StaticString<20 + MAX_PATH_LENGTH>("Could not create ", output_material_name));
			return false;
		}

		file.writeText("{\n\t\"shader\" : \"shaders/");
		file.writeText(material.shader);
		file.writeText(".shd\"\n");
		
		aiColor4D color;
		if (material.material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS)
		{
			file << ",\n\t\"color\" : [" << color.r << ", " << color.g << ", " << color.b << "]";
		}

		if (material.alpha_cutout) file << ",\n\t\"defines\" : [\"ALPHA_CUTOUT\"]";

		for (int i = 0; i < material.texture_count; ++i)
		{
			saveTexture(material.textures[i], source_mesh_dir, file, true);
		}

		file.write("}", 1);
		file.close();
		return true;
	}


	int task() override
	{
		static ConvertTask* that = nullptr;
		that = this;
		auto cmpMeshes = [](const void* a, const void* b) -> int {
			auto a_mesh = static_cast<const ImportMesh*>(a);
			auto b_mesh = static_cast<const ImportMesh*>(b);
			return a_mesh->lod - b_mesh->lod;
		};

		if (!m_dialog.m_meshes.empty())
		{
			qsort(&m_dialog.m_meshes[0], m_dialog.m_meshes.size(), sizeof(m_dialog.m_meshes[0]), cmpMeshes);
		}

		if (saveLumixPhysics() && saveLumixModel() && saveLumixMaterials() && saveLumixAnimations())
		{
			m_dialog.setMessage("Success.");
		}
		return 0;
	}


	int getNodeIndex(const aiBone* bone) const
	{
		for (int i = 0; i < m_nodes.size(); ++i)
		{
			if (bone->mName == m_nodes[i]->mName) return i;
		}
		return -1;
	}


	static void addBoneInfluence(SkinInfo& info, float weight, int bone_index)
	{
		if (info.index == 4)
		{
			int min = 0;
			for (int i = 1; i < 4; ++i)
			{
				if (info.weights[min] > info.weights[i]) min = i;
			}
			info.weights[min] = weight;
			info.bone_indices[min] = bone_index;
		}
		else
		{
			info.weights[info.index] = weight;
			info.bone_indices[info.index] = bone_index;
			++info.index;
		}
	}


	void fillSkinInfo(const ImportMesh& mesh, Array<SkinInfo>& infos) const
	{
		if (mesh.mesh->mNumBones == 0) return;

		infos.resize(mesh.map_to_input.size());

		for (unsigned int j = 0; j < mesh.mesh->mNumBones; ++j)
		{
			const aiBone* bone = mesh.mesh->mBones[j];
			int bone_index = getNodeIndex(bone);
			ASSERT(bone_index >= 0);
			for (unsigned int k = 0; k < bone->mNumWeights; ++k)
			{
				auto idx = mesh.map_from_input[bone->mWeights[k].mVertexId];
				ASSERT(idx == bone->mWeights[k].mVertexId);
				ASSERT(idx < (unsigned int)mesh.map_to_input.size());
				auto& info = infos[idx];
				addBoneInfluence(info, bone->mWeights[k].mWeight, bone_index);
			}
		}

		int invalid_vertices = 0;
		for (auto& info : infos)
		{
			float sum = info.weights[0] + info.weights[1] + info.weights[2] + info.weights[3];
			if (sum < 0.001f)
			{
				++invalid_vertices;
			}
			if (sum < 0.999f)
			{
				for (int i = 0; i < 4; ++i)
				{
					info.weights[i] /= sum;
				}
			}
		}
		if (invalid_vertices)
		{
			g_log_error.log("Editor") << "Mesh contains " << invalid_vertices
											 << " vertices not influenced by any bones.";
		}
	}


	static uint32 packuint32(uint8 _x, uint8 _y, uint8 _z, uint8 _w)
	{
		union {
			uint32 ui32;
			uint8 arr[4];
		} un;

		un.arr[0] = _x;
		un.arr[1] = _y;
		un.arr[2] = _z;
		un.arr[3] = _w;

		return un.ui32;
	}


	static uint32 packF4u(const Vec3& vec)
	{
		const uint8 xx = uint8(vec.x * 127.0f + 128.0f);
		const uint8 yy = uint8(vec.y * 127.0f + 128.0f);
		const uint8 zz = uint8(vec.z * 127.0f + 128.0f);
		const uint8 ww = uint8(0);
		return packuint32(xx, yy, zz, ww);
	}


	void sortParentFirst(aiNode* node, Array<aiNode*>& out)
	{
		if (!node) return;
		if (out.indexOf(node) >= 0) return;

		sortParentFirst(node->mParent, out);
		out.push(node);
	}


	void gatherNodes()
	{
		Array<aiNode*> tmp(m_dialog.m_editor.getAllocator());
		m_nodes.clear();
		for (auto& mesh : m_dialog.m_meshes)
		{
			if(!mesh.import) continue;
			for (unsigned int j = 0; j < mesh.mesh->mNumBones; ++j)
			{
				auto* node = getNode(mesh.mesh->mBones[j]->mName, mesh.scene->mRootNode);
				while (node && node->mNumMeshes == 0)
				{
					if (tmp.indexOf(node) >= 0) break;
					tmp.push(node);
					node = node->mParent;
				}
				if (node && tmp.indexOf(node) < 0) tmp.push(node);
			}
		}

		for (auto* node : tmp)
		{
			sortParentFirst(node, m_nodes);
		}
	}


	Vec3 fixOrientation(const aiVector3D& v) const
	{
		switch (m_dialog.m_model.orientation)
		{
			case ImportAssetDialog::Y_UP: return Vec3(v.x, v.y, v.z);
			case ImportAssetDialog::Z_UP: return Vec3(v.x, v.z, -v.y);
			case ImportAssetDialog::Z_MINUS_UP: return Vec3(v.x, -v.z, v.y);
			case ImportAssetDialog::X_MINUS_UP: return Vec3(v.y, -v.x, v.z);
		}
		ASSERT(false);
		return Vec3(v.x, v.y, v.z);
	}


	void writeIndices(FS::OsFile& file) const
	{
		if (areIndices16Bit())
		{
			for (auto& mesh : m_dialog.m_meshes)
			{
				if (mesh.import)
				{
					for (int i = 0; i < mesh.indices.size(); ++i)
					{
						uint16 index = mesh.indices[i];
						file.write(&index, sizeof(index));
					}
				}
			}

			if (m_dialog.m_model.create_billboard_lod)
			{
				uint16 indices[] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
				file.write(indices, sizeof(indices));
			}
		}
		else
		{
			for (auto& mesh : m_dialog.m_meshes)
			{
				if (mesh.import) file.write(&mesh.indices[0], mesh.indices.size() * sizeof(mesh.indices[0]));
			}

			if (m_dialog.m_model.create_billboard_lod)
			{
				uint32 indices[] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
				file.write(indices, sizeof(indices));
			}
		}
	}


	void writeVertices(FS::OsFile& file) const
	{

		Vec3 min(0, 0, 0);
		Vec3 max(0, 0, 0);
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (!mesh.import) continue;
			auto mesh_matrix = getGlobalTransform(getNode(mesh.scene, mesh.mesh, mesh.scene->mRootNode));
			auto normal_matrix = mesh_matrix;
			normal_matrix.a4 = normal_matrix.b4 = normal_matrix.c4 = 0;
			bool is_skinned = isSkinned(mesh.mesh);

			Array<SkinInfo> skin_infos(m_dialog.m_editor.getAllocator());
			fillSkinInfo(mesh, skin_infos);

			int skin_index = 0;
			for (auto j : mesh.map_to_input)
			{
				if (is_skinned)
				{
					file.write((const char*)skin_infos[skin_index].weights, sizeof(skin_infos[j].weights));
					file.write((const char*)skin_infos[skin_index].bone_indices, sizeof(skin_infos[j].bone_indices));
					++skin_index;
				}

				auto v = mesh_matrix * mesh.mesh->mVertices[j];

				Vec3 position = fixOrientation(v);
				position *= m_scale;

				min.x = Math::minimum(min.x, position.x);
				min.y = Math::minimum(min.y, position.y);
				min.z = Math::minimum(min.z, position.z);
				max.x = Math::maximum(max.x, position.x);
				max.y = Math::maximum(max.y, position.y);
				max.z = Math::maximum(max.z, position.z);

				file.write((const char*)&position, sizeof(position));

				if (mesh.mesh->mColors[0])
				{
					auto assimp_color = mesh.mesh->mColors[0][j];
					uint8 color[4];
					color[0] = uint8(assimp_color.r * 255);
					color[1] = uint8(assimp_color.g * 255);
					color[2] = uint8(assimp_color.b * 255);
					color[3] = uint8(assimp_color.a * 255);
					file.write(color, sizeof(color));
				}

				auto tmp_normal = normal_matrix * mesh.mesh->mNormals[j];
				tmp_normal.Normalize();
				Vec3 normal = fixOrientation(tmp_normal);
				uint32 int_normal = packF4u(normal);
				file.write((const char*)&int_normal, sizeof(int_normal));

				if (mesh.mesh->mTangents)
				{
					auto tmp_tangent = normal_matrix * mesh.mesh->mTangents[j];
					tmp_tangent.Normalize();
					Vec3 tangent = fixOrientation(tmp_tangent);
					uint32 int_tangent = packF4u(tangent);
					file.write((const char*)&int_tangent, sizeof(int_tangent));
				}

				if (mesh.mesh->mTextureCoords[0])
				{
					auto uv = mesh.mesh->mTextureCoords[0][j];
					uv.y = -uv.y;
					file.write((const char*)&uv, sizeof(uv.x) + sizeof(uv.y));
				}
			}
		}

		if (m_dialog.m_model.create_billboard_lod)
		{
			Vec3 size = max - min;
			float u[] = { 0.0f, 0.5f, 1.0f };
			float v[] = { 0.0f, 1.0f };
			if (size.x + size.z < size.y)
			{
				int width = int(TEXTURE_SIZE / size.y * (size.x + size.z));
				int ceiled = ceilPowOf2(width);
				int diff = ceiled - width;
				u[0] = diff * 0.5f / float(ceiled);
				u[2] = 1 - u[0];
				u[1] = u[0] + size.x / (size.x + size.z) * (1 - 2 * u[0]);
			}
			else
			{
				u[1] = size.x / (size.x + size.z);
				
				float t = size.y / (size.x + size.z);

				v[0] = 0.5f - t * 0.5f;
				v[1] = 0.5f + t * 0.5f;
			}
			BillboardVertex vertices[8] = {
				{ { min.x, min.y, 0 }, { 128, 255, 128, 0 }, { 255, 128, 128, 0 }, { u[0], v[1] } },
				{ { max.x, min.y, 0 }, { 128, 255, 128, 0 }, { 255, 128, 128, 0 }, { u[1], v[1] } },
				{ { max.x, max.y, 0 }, { 128, 255, 128, 0 }, { 255, 128, 128, 0 }, { u[1], v[0] } },
				{ { min.x, max.y, 0 }, { 128, 255, 128, 0 }, { 255, 128, 128, 0 }, { u[0], v[0] } },

				{ { 0, min.y, min.z }, { 128, 255, 128, 0 }, { 255, 128, 128, 0 }, { u[1], v[1] } },
				{ { 0, min.y, max.z }, { 128, 255, 128, 0 }, { 255, 128, 128, 0 }, { u[2], v[1] } },
				{ { 0, max.y, max.z }, { 128, 255, 128, 0 }, { 255, 128, 128, 0 }, { u[2], v[0] } },
				{ { 0, max.y, min.z }, { 128, 255, 128, 0 }, { 255, 128, 128, 0 }, { u[1], v[0] } }
			};
			file.write(vertices, sizeof(vertices));
		} 
	}


	void writeGeometry(FS::OsFile& file) const
	{
		int32 indices_count = 0;
		int32 vertices_size = 0;
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (!mesh.import) continue;
			indices_count += mesh.indices.size();
			vertices_size += mesh.map_to_input.size() * getVertexSize(mesh.mesh);
		}

		if (m_dialog.m_model.create_billboard_lod)
		{
			indices_count += 4*3;
			vertices_size += 8 * sizeof(BillboardVertex);
		}

		file.write((const char*)&indices_count, sizeof(indices_count));
		writeIndices(file);

		file.write((const char*)&vertices_size, sizeof(vertices_size));
		writeVertices(file);
	}


	static int getAttributeCount(const aiMesh* mesh)
	{
		int count = 2; // position, normal
		if (mesh->HasTextureCoords(0)) ++count;
		if (isSkinned(mesh)) count += 2;
		if (mesh->HasVertexColors(0)) ++count;
		if (mesh->mTangents) ++count;
		return count;
	}


	static int getVertexSize(const aiMesh* mesh)
	{
		static const int POSITION_SIZE = sizeof(float) * 3;
		static const int NORMAL_SIZE = sizeof(uint8) * 4;
		static const int TANGENT_SIZE = sizeof(uint8) * 4;
		static const int UV_SIZE = sizeof(float) * 2;
		static const int COLOR_SIZE = sizeof(uint8) * 4;
		static const int BONE_INDICES_WEIGHTS_SIZE = sizeof(float) * 4 + sizeof(uint16) * 4;
		int size = POSITION_SIZE + NORMAL_SIZE;
		if (mesh->HasTextureCoords(0)) size += UV_SIZE;
		if (mesh->mTangents) size += TANGENT_SIZE;
		if (mesh->HasVertexColors(0)) size += COLOR_SIZE;
		if (isSkinned(mesh)) size += BONE_INDICES_WEIGHTS_SIZE;
		return size;
	}


	void writeBillboardMesh(FS::OsFile& file, int32 attribute_array_offset, int32 indices_offset)
	{
		if (!m_dialog.m_model.create_billboard_lod) return;

		int vertex_size = sizeof(BillboardVertex);
		StaticString<MAX_PATH_LENGTH + 10> material_name(m_dialog.m_mesh_output_filename, "_billboard");
		int32 length = stringLength(material_name);
		file.write((const char*)&length, sizeof(length));
		file.write(material_name, length);

		file.write((const char*)&attribute_array_offset, sizeof(attribute_array_offset));
		int32 attribute_array_size = 8 * vertex_size;
		attribute_array_offset += attribute_array_size;
		file.write((const char*)&attribute_array_size, sizeof(attribute_array_size));

		file.write((const char*)&indices_offset, sizeof(indices_offset));
		int32 mesh_tri_count = 4;
		indices_offset += mesh_tri_count * 3;
		file.write((const char*)&mesh_tri_count, sizeof(mesh_tri_count));

		const char* mesh_name = "billboard";
		length = stringLength(mesh_name);

		file.write((const char*)&length, sizeof(length));
		file.write(mesh_name, length);
	}


	void writeMeshes(FS::OsFile& file)
	{
		int32 mesh_count = 0;
		for (int i = 0; i < m_dialog.m_meshes.size(); ++i)
		{
			if (m_dialog.m_meshes[i].import) ++mesh_count;
		}
		if (m_dialog.m_model.create_billboard_lod) ++mesh_count;

		file.write((const char*)&mesh_count, sizeof(mesh_count));
		int32 attribute_array_offset = 0;
		int32 indices_offset = 0;
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (!mesh.import) continue;

			int vertex_size = getVertexSize(mesh.mesh);
			aiString material_name;
			mesh.scene->mMaterials[mesh.mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, material_name);
			int32 length = stringLength(material_name.C_Str());
			file.write((const char*)&length, sizeof(length));
			file.write((const char*)material_name.C_Str(), length);

			file.write((const char*)&attribute_array_offset, sizeof(attribute_array_offset));
			int32 attribute_array_size = mesh.map_to_input.size() * vertex_size;
			attribute_array_offset += attribute_array_size;
			file.write((const char*)&attribute_array_size, sizeof(attribute_array_size));

			file.write((const char*)&indices_offset, sizeof(indices_offset));
			int32 mesh_tri_count = mesh.indices.size() / 3;
			indices_offset += mesh.indices.size();
			file.write((const char*)&mesh_tri_count, sizeof(mesh_tri_count));

			aiString mesh_name = getMeshName(mesh.scene, mesh.mesh);
			length = stringLength(mesh_name.C_Str());

			file.write((const char*)&length, sizeof(length));
			file.write((const char*)mesh_name.C_Str(), length);
		}

		writeBillboardMesh(file, attribute_array_offset, indices_offset);
	}


	static void writeAttribute(bgfx::Attrib::Enum attrib, FS::OsFile& file)
	{
		int32 tmp = attrib;
		file.write(&tmp, sizeof(tmp));
	}


	void writeLods(FS::OsFile& file) const
	{
		int32 lod_count = 1;
		int32 last_mesh_idx = -1;
		int32 lods[8] = {};
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (!mesh.import) continue;

			++last_mesh_idx;
			if (mesh.lod >= lengthOf(m_dialog.m_model.lods)) continue;
			lod_count = mesh.lod + 1;
			lods[mesh.lod] = last_mesh_idx;
		}

		if (m_dialog.m_model.create_billboard_lod)
		{
			lods[lod_count] = last_mesh_idx + 1;
			++lod_count;
		}

		file.write((const char*)&lod_count, sizeof(lod_count));

		for (int i = 0; i < lod_count; ++i)
		{
			int32 to_mesh = lods[i];
			file.write((const char*)&to_mesh, sizeof(to_mesh));
			float factor = m_dialog.m_model.lods[i] < 0 ? FLT_MAX : m_dialog.m_model.lods[i] * m_dialog.m_model.lods[i];
			file.write((const char*)&factor, sizeof(factor));
		}
	}


	aiMatrix4x4 getGlobalTransform(aiNode* node) const
	{
		aiMatrix4x4 mtx;
		while (node)
		{
			mtx = node->mTransformation * mtx;
			node = node->mParent;
		}
		return mtx;
	}


	static aiNode* getNode(const aiScene* scene, aiMesh* mesh, aiNode* node)
	{
		for (unsigned int i = 0; i < node->mNumMeshes; ++i)
		{
			if (mesh == scene->mMeshes[node->mMeshes[i]]) return node;
		}

		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			auto* x = getNode(scene, mesh, node->mChildren[i]);
			if (x) return x;
		}
		return nullptr;
	}


	aiNode* getNode(const aiString& node_name, aiNode* node) const
	{
		if (node->mName == node_name) return node;

		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			auto* x = getNode(node_name, node->mChildren[i]);
			if (x) return x;
		}

		return nullptr;
	}


	static aiBone* getBone(const aiScene* scene, aiNode* node)
	{
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
		{
			auto* mesh = scene->mMeshes[i];
			for (unsigned int j = 0; j < mesh->mNumBones; ++j)
			{
				if (mesh->mBones[j]->mName == node->mName) return mesh->mBones[j];
			}
		}
		return nullptr;
	}


	static aiNode* getMeshNode(const aiScene* scene, aiNode* node)
	{
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
		{
			auto* mesh = scene->mMeshes[i];
			for (unsigned int j = 0; j < mesh->mNumBones; ++j)
			{
				if (mesh->mBones[j]->mName == node->mName)
				{
					return getNode(scene, mesh, scene->mRootNode);
				}
			}
		}
		return nullptr;
	}


	const aiScene* getNodeScene(aiNode* node)
	{
		auto* root = node;
		while (root->mParent) root = root->mParent;

		for (auto& i : m_dialog.m_importers)
		{
			if (i.GetScene()->mRootNode == root) return i.GetScene();
		}
		return nullptr;
	}


	void writeSkeleton(FS::OsFile& file)
	{
		int32 count = m_nodes.size();
		if (count == 1) count = 0;
		file.write((const char*)&count, sizeof(count));

		for (auto* node : m_nodes)
		{
			auto* scene = getNodeScene(node);
			int32 len = stringLength(node->mName.C_Str());
			file.write((const char*)&len, sizeof(len));
			file.write(node->mName.C_Str(), node->mName.length);

			if (node->mParent)
			{
				int32 len = stringLength(node->mParent->mName.C_Str());
				file.write((const char*)&len, sizeof(len));
				file.write(node->mParent->mName.C_Str(), node->mParent->mName.length);
			}
			else
			{
				int32 len = 0;
				file.write((const char*)&len, sizeof(len));
			}

			aiQuaterniont<float> rot;
			aiVector3t<float> pos;
			aiVector3t<float> scale;
			auto bone = getBone(scene, node);
			if (bone)
			{
				aiMatrix4x4 mtx;
				mtx = bone->mOffsetMatrix;
				mtx.Inverse();
				mtx = getGlobalTransform(getMeshNode(scene, node)) * mtx;
				mtx.Decompose(scale, rot, pos);
			}
			else
			{
				getGlobalTransform(node).Decompose(scale, rot, pos);
			}
			pos *= m_dialog.m_model.mesh_scale;
			file.write((const char*)&pos, sizeof(pos));
			file.write((const char*)&rot.x, sizeof(rot.x));
			file.write((const char*)&rot.y, sizeof(rot.y));
			file.write((const char*)&rot.z, sizeof(rot.z));
			file.write((const char*)&rot.w, sizeof(rot.w));
		}
	}


	void writePhysicsHeader(FS::OsFile& file) const
	{
		PhysicsGeometry::Header header;
		header.m_magic = PhysicsGeometry::HEADER_MAGIC;
		header.m_version = (uint32)PhysicsGeometry::Versions::LAST;
		header.m_convex = (uint32)m_dialog.m_model.make_convex;
		file.write((const char*)&header, sizeof(header));
	}


	bool saveLumixPhysics()
	{
		bool any = false;
		for (auto& m : m_dialog.m_meshes)
		{
			if (m.import_physics)
			{
				any = true;
				break;
			}
		}

		if (!any) return true;

		m_dialog.setImportMessage("Importing physics...", -1);
		char filename[MAX_PATH_LENGTH];
		PathUtils::getBasename(filename, sizeof(filename), m_dialog.m_source);
		catString(filename, ".phy");
		PathBuilder phy_path(m_dialog.m_output_dir);
		phy_path << "/" << filename;
		FS::OsFile file;
		if (!file.open(phy_path, FS::Mode::CREATE_AND_WRITE, m_dialog.m_editor.getAllocator()))
		{
			g_log_error.log("Editor") << "Could not create file " << phy_path;
			return false;
		}

		writePhysicsHeader(file);
		int32 count = 0;
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (mesh.import_physics) count += (int32)mesh.mesh->mNumVertices;
		}
		file.write((const char*)&count, sizeof(count));
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (mesh.import_physics)
			{
				file.write(
					(const char*)mesh.mesh->mVertices, sizeof(mesh.mesh->mVertices[0]) * mesh.mesh->mNumVertices);
			}
		}

		if (!m_dialog.m_model.make_convex) writePhysiscTriMesh(file);
		file.close();

		return true;
	}


	void writePhysiscTriMesh(FS::OsFile& file)
	{
		int count = 0;
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (mesh.import) count += (int)mesh.mesh->mNumFaces * 3;
		}
		file.write((const char*)&count, sizeof(count));
		int offset = 0;
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (!mesh.import_physics) continue;
			for (unsigned int j = 0; j < mesh.mesh->mNumFaces; ++j)
			{
				ASSERT(mesh.mesh->mFaces[j].mNumIndices == 3);
				uint32 index = mesh.mesh->mFaces[j].mIndices[0] + offset;
				file.write((const char*)&index, sizeof(index));
				index = mesh.mesh->mFaces[j].mIndices[1] + offset;
				file.write((const char*)&index, sizeof(index));
				index = mesh.mesh->mFaces[j].mIndices[2] + offset;
				file.write((const char*)&index, sizeof(index));
			}
			offset += mesh.mesh->mNumVertices;
		}
	}


	bool checkModel() const
	{
		int imported_meshes = 0;
		int skinned_meshes = 0;
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (!mesh.import) continue;
			++imported_meshes;
			if (isSkinned(mesh.mesh)) ++skinned_meshes;
			if (!mesh.mesh->HasNormals())
			{
				m_dialog.setMessage(
					StaticString<256>("Mesh ", getMeshName(mesh.scene, mesh.mesh).C_Str(), " has no normals."));
				return false;
			}
			if (!mesh.mesh->HasPositions())
			{
				m_dialog.setMessage(StaticString<256>(
					"Mesh ", getMeshName(mesh.scene, mesh.mesh).C_Str(), " has no positions."));
				return false;
			}
		}
		if (skinned_meshes != 0 && skinned_meshes != imported_meshes)
		{
			m_dialog.setMessage("Not all meshes have bones");
			return false;
		}
		return true;
	}


	bool areIndices16Bit() const
	{
		for(auto& mesh : m_dialog.m_meshes)
		{
			if(mesh.import && mesh.indices.size() > (1 << 16))
			{
				return false;
			}
		}
		return true;
	}


	void writeModelHeader(FS::OsFile& file) const
	{
		Model::FileHeader header;
		header.magic = Model::FILE_MAGIC;
		header.version = (uint32)Model::FileVersion::LATEST;
		file.write((const char*)&header, sizeof(header));
		uint32 flags = areIndices16Bit() ? (uint32)Model::Flags::INDICES_16BIT : 0;
		file.write((const char*)&flags, sizeof(flags));

		const aiMesh* mesh = nullptr;
		for (const auto& i : m_dialog.m_meshes)
		{
			if (i.import)
			{
				mesh = i.mesh;
				break;
			}
		}
		ASSERT(mesh);
		int32 attribute_count = getAttributeCount(mesh);
		file.write((const char*)&attribute_count, sizeof(attribute_count));

		if (isSkinned(mesh))
		{
			writeAttribute(bgfx::Attrib::Weight, file);
			writeAttribute(bgfx::Attrib::Indices, file);
		}

		writeAttribute(bgfx::Attrib::Position, file);
		if (mesh->HasVertexColors(0)) writeAttribute(bgfx::Attrib::Color0, file);
		writeAttribute(bgfx::Attrib::Normal, file);
		if (mesh->mTangents) writeAttribute(bgfx::Attrib::Tangent, file);
		if (mesh->HasTextureCoords(0)) writeAttribute(bgfx::Attrib::TexCoord0, file);
	}


	bool saveLumixModel()
	{
		ASSERT(m_dialog.m_output_dir[0] != '\0');
		ASSERT(m_dialog.m_mesh_output_filename[0] != '\0');
		bool import_any = false;
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (mesh.import)
			{
				import_any = true;
				break;
			}
		}
		if (!import_any) return true;
		if (!checkModel()) return false;

		m_dialog.setImportMessage("Importing model...", -1);
		PlatformInterface::makePath(m_dialog.m_output_dir);
		if (m_dialog.m_texture_output_dir[0]) PlatformInterface::makePath(m_dialog.m_texture_output_dir);

		PathBuilder path(m_dialog.m_output_dir);
		path << "/" << m_dialog.m_mesh_output_filename << ".msh";

		IAllocator& allocator = m_dialog.m_editor.getAllocator();
		FS::OsFile file;

		if (!file.open(path, FS::Mode::CREATE_AND_WRITE, allocator))
		{
			m_dialog.setMessage(StaticString<MAX_PATH_LENGTH + 15>("Failed to open ", path));
			return false;
		}

		gatherNodes();

		uint32 preprocess_flags = 0;
		if (m_dialog.m_model.remove_doubles) preprocess_flags |= (uint32)Preprocesses::REMOVE_DOUBLES;
		for (auto& mesh : m_dialog.m_meshes)
		{
			if (mesh.import) preprocessMesh(mesh, preprocess_flags, allocator);
		}

		writeModelHeader(file);
		writeMeshes(file);
		writeGeometry(file);
		writeSkeleton(file);
		writeLods(file);

		file.close();
		return true;
	}

	ImportAssetDialog& m_dialog;
	Array<aiNode*> m_nodes;
	float m_scale;

}; // struct ConvertTask


ImportAssetDialog::ImportAssetDialog(StudioApp& app)
	: m_metadata(*app.getMetadata())
	, m_task(nullptr)
	, m_editor(*app.getWorldEditor())
	, m_is_converting(false)
	, m_is_importing(false)
	, m_is_importing_texture(false)
	, m_mutex(false)
	, m_saved_textures(app.getWorldEditor()->getAllocator())
	, m_convert_to_dds(false)
	, m_convert_to_raw(false)
	, m_raw_texture_scale(1)
	, m_meshes(app.getWorldEditor()->getAllocator())
	, m_materials(app.getWorldEditor()->getAllocator())
	, m_importers(app.getWorldEditor()->getAllocator())
	, m_sources(app.getWorldEditor()->getAllocator())
	, m_animations(app.getWorldEditor()->getAllocator())
{
	m_model.make_convex = false;
	m_model.mesh_scale = 1;
	m_model.remove_doubles = false;
	m_model.create_billboard_lod = false;
	m_model.lods[0] = -10;
	m_model.lods[1] = -100;
	m_model.lods[2] = -1000;
	m_model.lods[3] = -10000;
	m_model.orientation = Y_UP;
	m_is_opened = false;
	m_message[0] = '\0';
	m_import_message[0] = '\0';
	m_task = nullptr;
	m_source[0] = '\0';
	m_output_dir[0] = '\0';
	m_mesh_output_filename[0] = '\0';
	m_texture_output_dir[0] = '\0';
	copyString(m_last_dir, m_editor.getEngine().getDiskFileDevice()->getBasePath());

	m_action = LUMIX_NEW(m_editor.getAllocator(), Action)("Import Asset", "import_asset");
	m_action->func.bind<ImportAssetDialog, &ImportAssetDialog::onAction>(this);
	m_action->is_selected.bind<ImportAssetDialog, &ImportAssetDialog::isOpened>(this);

	LuaWrapper::createSystemFunction(m_editor.getEngine().getState(), "Editor", "importAsset", &::importAsset);
	LuaWrapper::createSystemVariable(m_editor.getEngine().getState(), "Editor", "import_asset_dialog", this);
}


bool ImportAssetDialog::isOpened() const
{
	return m_is_opened;
}


ImportAssetDialog::~ImportAssetDialog()
{
	LuaWrapper::createSystemVariable(m_editor.getEngine().getState(), "Editor", "import_asset_dialog", nullptr);
	if (m_task)
	{
		m_task->destroy();
		LUMIX_DELETE(m_editor.getAllocator(), m_task);
	}
}


static bool isImage(const char* path)
{
	char ext[10];
	PathUtils::getExtension(ext, sizeof(ext), path);

	static const char* image_extensions[] = {
		"dds", "jpg", "jpeg", "png", "tga", "bmp", "psd", "gif", "hdr", "pic", "pnm"};
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
		m_animations.clear();
		m_materials.clear();
		m_meshes.clear();
		m_importers.clear();
		return true;
	}

	ASSERT(!m_task);
	m_importers.emplace();
	m_sources.emplace(m_source);
	setImportMessage("Importing...", -1);
	m_is_importing = true;
	m_task = LUMIX_NEW(m_editor.getAllocator(), ImportTask)(*this);
	m_task->create("ImportAssetTask");
	return true;
}


void ImportAssetDialog::setMessage(const char* message)
{
	MT::SpinLock lock(m_mutex);
	copyString(m_message, message);
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
	PathBuilder model_path(m_output_dir, "/", m_mesh_output_filename, ".msh");
	char tmp[MAX_PATH_LENGTH];
	PathUtils::normalize(model_path, tmp, lengthOf(tmp));
	uint32 model_path_hash = crc32(tmp);

	OutputBlob blob(m_editor.getAllocator());
	blob.reserve(1024);
	blob.write(&m_model, sizeof(m_model));
	blob.write(m_meshes.size());
	for (auto& i : m_meshes)
	{
		blob.write(i.import);
		blob.write(i.import_physics);
		blob.write(i.lod);
	}
	blob.write(m_materials.size());
	for (auto& i : m_materials)
	{
		blob.write(i.import);
		blob.write(i.alpha_cutout);
		blob.write(i.shader);
		blob.write(i.texture_count);
		for (int j = 0; j < i.texture_count; ++j)
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


void ImportAssetDialog::convert(bool use_ui)
{
	ASSERT(!m_task);

	for (auto& material : m_materials)
	{
		for (int i = 0; i < material.texture_count; ++i)
		{
			if (!material.textures[i].is_valid && material.textures[i].import)
			{
				if(use_ui) ImGui::OpenPopup("Invalid texture");
				return;
			}
		}
	}

	saveModelMetadata();

	setImportMessage("Converting...", -1);
	m_is_converting = true;
	m_task = LUMIX_NEW(m_editor.getAllocator(), ConvertTask)(*this, m_model.mesh_scale);
	m_task->create("ConvertAssetTask");
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
	uint32 hash = crc32(dest_path);

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
	label << m_materials.size() << ")###Materials";
	if (!ImGui::CollapsingHeader(label)) return;

	ImGui::Indent();
	if (ImGui::Button("Import all materials"))
	{
		for (auto& mat : m_materials) mat.import = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Do not import any materials"))
	{
		for (auto& mat : m_materials) mat.import = false;
	}
	if (ImGui::Button("Import all textures"))
	{
		for (auto& mat : m_materials)
		{
			for (auto& tex : mat.textures) tex.import = true;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Do not import any textures"))
	{
		for (auto& mat : m_materials)
		{
			for (auto& tex : mat.textures) tex.import = false;
		}
	}
	for (auto& mat : m_materials)
	{
		aiString material_name;
		mat.material->Get(AI_MATKEY_NAME, material_name);
		if (ImGui::TreeNode(mat.material, "%s", material_name.C_Str()))
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
			for (int i = 0; i < mat.texture_count; ++i)
			{
				ImGui::Text("%s", mat.textures[i].path);
				ImGui::NextColumn();
				ImGui::Checkbox(StaticString<20>("###imp", i), &mat.textures[i].import);
				ImGui::NextColumn();
				ImGui::Checkbox(StaticString<20>("###dds", i), &mat.textures[i].to_dds);
				ImGui::NextColumn();
				if (ImGui::Button(StaticString<50>("Browse###brw", i)))
				{
					if (PlatformInterface::getOpenFilename(
							mat.textures[i].src, lengthOf(mat.textures[i].src), "All\0*.*\0", nullptr))
					{
						mat.textures[i].is_valid = true;
					}
				}
				ImGui::SameLine();
				ImGui::Text("%s", mat.textures[i].src);
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
	for (int i = 0; i < lengthOf(m_model.lods); ++i)
	{
		bool b = m_model.lods[i] < 0;
		if (ImGui::Checkbox(StaticString<20>("Infinite###lod_inf", i), &b))
		{
			m_model.lods[i] *= -1;
		}
		if (m_model.lods[i] >= 0)
		{
			ImGui::SameLine();
			ImGui::DragFloat(StaticString<10>("LOD ", i), &m_model.lods[i], 1.0f, 1.0f, FLT_MAX);
		}
	}
}


void ImportAssetDialog::onAnimationsGUI()
{
	StaticString<30> label("Animations (");
	label << m_animations.size() << ")###Animations";
	if (!ImGui::CollapsingHeader(label)) return;

	ImGui::Indent();
	ImGui::Columns(2);
	
	ImGui::Text("Name");
	ImGui::NextColumn();
	ImGui::Text("Import");
	ImGui::NextColumn();
	ImGui::Separator();

	ImGui::PushID("anims");
	for (int i = 0; i < m_animations.size(); ++i)
	{
		auto& animation = m_animations[i];
		ImGui::PushID(i);
		ImGui::InputText("", animation.output_filename, lengthOf(animation.output_filename));
		ImGui::NextColumn();
		ImGui::Checkbox("", &animation.import);
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
	label << m_meshes.size() << ")###Meshes";
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

	for (auto& mesh : m_meshes)
	{
		const char* name = mesh.mesh->mName.C_Str();
		if (name[0] == 0) name = getMeshName(mesh.scene, mesh.mesh).C_Str();
		ImGui::Text("%s", name);
		ImGui::NextColumn();

		auto* material = mesh.scene->mMaterials[mesh.mesh->mMaterialIndex];
		aiString material_name;
		material->Get(AI_MATKEY_NAME, material_name);
		ImGui::Text("%s", material_name.C_Str());
		ImGui::NextColumn();

		ImGui::Checkbox(StaticString<30>("###mesh", (uint64)&mesh), &mesh.import);
		if (ImGui::GetIO().MouseClicked[1] && ImGui::IsItemHovered()) ImGui::OpenPopup("ContextMesh");
		ImGui::NextColumn();
		ImGui::Checkbox(StaticString<30>("###phy", (uint64)&mesh), &mesh.import_physics);
		if (ImGui::GetIO().MouseClicked[1] && ImGui::IsItemHovered()) ImGui::OpenPopup("ContextPhy");
		ImGui::NextColumn();
		ImGui::Combo(StaticString<30>("###lod", (uint64)&mesh), &mesh.lod, "LOD 1\0LOD 2\0LOD 3\0LOD 4\0");
		ImGui::NextColumn();
	}
	ImGui::Columns();
	ImGui::Unindent();
	if (ImGui::BeginPopup("ContextMesh"))
	{
		if (ImGui::Selectable("Select all"))
		{
			for (auto& mesh : m_meshes) mesh.import = true;
		}
		if (ImGui::Selectable("Deselect all"))
		{
			for (auto& mesh : m_meshes) mesh.import = false;
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("ContextPhy"))
	{
		if (ImGui::Selectable("Select all"))
		{
			for (auto& mesh : m_meshes) mesh.import_physics = true;
		}
		if (ImGui::Selectable("Deselect all"))
		{
			for (auto& mesh : m_meshes) mesh.import_physics = false;
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

	if (ImGui::Button("Import texture")) importTexture();
}


static void preprocessBillboard(uint32* pixels, int width, int height, IAllocator& allocator)
{
	struct DistanceFieldCell
	{
		uint32 distance;
		uint32 color;
	};

	Array<DistanceFieldCell> distance_field(allocator);
	distance_field.resize(width * height);

	static const uint32 ALPHA_MASK = 0xff000000;
	
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
	int texture_size)
{
	auto& engine = dialog.getEditor().getEngine();
	auto& universe = engine.createUniverse();

	auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
	if (!renderer) return false;

	auto* render_scene = static_cast<RenderScene*>(universe.getScene(crc32("renderer")));
	if (!render_scene) return false;

	auto* pipeline = Pipeline::create(*renderer, Path("pipelines/preview.lua"), engine.getAllocator());
	pipeline->load();

	auto mesh_entity = universe.createEntity({0, 0, 0}, {0, 0, 0, 0});
	static const auto RENDERABLE_TYPE = PropertyRegister::getComponentType("renderable");
	auto mesh_cmp = render_scene->createComponent(RENDERABLE_TYPE, mesh_entity);
	render_scene->setRenderablePath(mesh_cmp, mesh_path);

	auto mesh_side_entity = universe.createEntity({0, 0, 0}, {Vec3(0, 1, 0), Math::PI * 0.5f});
	auto mesh_side_cmp = render_scene->createComponent(RENDERABLE_TYPE, mesh_side_entity);
	render_scene->setRenderablePath(mesh_side_cmp, mesh_path);

	auto light_entity = universe.createEntity({0, 0, 0}, {0, 0, 0, 0});
	static const auto GLOBAL_LIGHT_TYPE = PropertyRegister::getComponentType("global_light");
	auto light_cmp = render_scene->createComponent(GLOBAL_LIGHT_TYPE, light_entity);
	render_scene->setGlobalLightIntensity(light_cmp, 0);
	render_scene->setLightAmbientIntensity(light_cmp, 1);

	while (engine.getFileSystem().hasWork()) engine.getFileSystem().updateAsyncTransactions();

	auto* model = render_scene->getRenderableModel(mesh_cmp);
	auto* lods = model->getLODs();
	lods[0].distance = FLT_MAX;
	AABB aabb = model->getAABB();
	Vec3 size = aabb.max - aabb.min;
	universe.setPosition(mesh_side_entity, { aabb.max.x - aabb.min.z, 0, 0 });
	Vec3 camera_pos(
		(aabb.min.x + aabb.max.x + size.z) * 0.5f, (aabb.max.y + aabb.min.y) * 0.5f, aabb.max.z + 5);
	auto camera_entity = universe.createEntity(camera_pos, { 0, 0, 0, 1 });
	static const auto CAMERA_TYPE = PropertyRegister::getComponentType("camera");
	auto camera_cmp = render_scene->createComponent(CAMERA_TYPE, camera_entity);
	render_scene->setCameraOrtho(camera_cmp, true);
	render_scene->setCameraSlot(camera_cmp, "main");
	int width, height;
	if (size.x + size.z > size.y)
	{
		width = texture_size;
		int nonceiled_height = int(width / (size.x + size.z) * size.y);
		height = ceilPowOf2(nonceiled_height);
		render_scene->setCameraOrthoSize(camera_cmp, size.y * height / nonceiled_height *  0.5f);
	}
	else
	{
		height = texture_size;
		width = ceilPowOf2(int(height * (size.x + size.z) / size.y));
		render_scene->setCameraOrthoSize(camera_cmp, size.y * 0.5f);
	}

	pipeline->setScene(render_scene);
	pipeline->setViewport(0, 0, width, height);
	pipeline->render();

	bgfx::TextureHandle texture =
		bgfx::createTexture2D(width, height, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_READ_BACK);

	renderer->viewCounterAdd();
	bgfx::touch(renderer->getViewCounter());
	bgfx::setViewName(renderer->getViewCounter(), "billboard_blit");
	bgfx::TextureHandle color_renderbuffer = pipeline->getFramebuffer("default")->getRenderbufferHandle(0);
	bgfx::blit(renderer->getViewCounter(), texture, 0, 0, color_renderbuffer);

	renderer->viewCounterAdd();
	bgfx::setViewName(renderer->getViewCounter(), "billboard_read");
	Array<uint8> data(engine.getAllocator());
	data.resize(width * height * 4);
	bgfx::readTexture(texture, &data[0]);
	bgfx::touch(renderer->getViewCounter());
	bgfx::frame(); // submit
	bgfx::frame(); // wait for gpu

	preprocessBillboard((uint32*)&data[0], width, height, engine.getAllocator());
	saveAsDDS(dialog, "billboard_generator", (uint8*)&data[0], width, height, true, out_path.c_str());
	bgfx::destroyTexture(texture);
	Pipeline::destroy(pipeline);
	engine.destroyUniverse(universe);
	return true;
}


static ImportMaterial* getMatchingMaterial(lua_State* L, ImportMaterial* materials, int count)
{
	auto x = lua_gettop(L);
	if (lua_getfield(L, -1, "matching") == LUA_TFUNCTION)
	{
		for (int i = 0; i < count; ++i)
		{
			lua_pushvalue(L, -1); // duplicate "matching"
			ImportMaterial& material = materials[i];
			aiString material_name;
			material.material->Get(AI_MATKEY_NAME, material_name);
			LuaWrapper::pushLua(L, i);
			LuaWrapper::pushLua(L, material_name.C_Str());
			if (lua_pcall(L, 2, 1, 0) != LUA_OK)
			{
				g_log_error.log("Editor") << "getMatchingMaterial" << ": " << lua_tostring(L, -1);
				lua_pop(L, 1);
			}
			else
			{
				bool is_matching = LuaWrapper::toType<bool>(L, -1);
				lua_pop(L, 1);
				if (is_matching)
				{
					lua_pop(L, 1); // "matching"
					auto u = lua_gettop(L);
					return &material;
				}
			}
		}
	}
	else
	{
		g_log_error.log("Editor") << "No \"matching\" found in table or it is not a function";
	}
	lua_pop(L, 1); // "matching"
	return nullptr;
}


int ImportAssetDialog::importAsset(lua_State* L)
{
	m_importers.clear();
	m_animations.clear();
	m_materials.clear();
	m_meshes.clear();
	m_mesh_output_filename[0] = '\0';
	m_is_opened = true;

	LuaWrapper::checkTableArg(L, 2);
	if (lua_getfield(L, 2, "output_dir") == LUA_TSTRING)
	{
		copyString(m_output_dir, LuaWrapper::toType<const char*>(L, -1));
	}
	lua_pop(L, 1);
	if (lua_getfield(L, 2, "create_billboard") == LUA_TBOOLEAN)
	{
		m_model.create_billboard_lod = LuaWrapper::toType<bool>(L, -1);
	}
	lua_pop(L, 1);
	if (lua_getfield(L, 2, "remove_doubles") == LUA_TBOOLEAN)
	{
		m_model.remove_doubles = LuaWrapper::toType<bool>(L, -1);
	}
	lua_pop(L, 1);
	if (lua_getfield(L, 2, "scale") == LUA_TNUMBER)
	{
		m_model.mesh_scale = LuaWrapper::toType<float>(L, -1);
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 2, "output_dir") == LUA_TSTRING)
	{
		copyString(m_output_dir, LuaWrapper::toType<const char*>(L, -1));
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 2, "lods") == LUA_TTABLE)
	{
		lua_pushnil(L);
		int lod_index = 0;
		while (lua_next(L, -2) != 0)
		{
			if (lod_index >= lengthOf(m_model.lods))
			{
				g_log_error.log("Editor") << "Only " << lengthOf(m_model.lods) << " supported";
				lua_pop(L, 1);
				break;
			}

			m_model.lods[lod_index] = LuaWrapper::toType<float>(L, -1);
			++lod_index;
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 2, "texture_output_dir") == LUA_TSTRING)
	{
		copyString(m_texture_output_dir, LuaWrapper::toType<const char*>(L, -1));
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 2, "srcs") == LUA_TTABLE)
	{
		lua_pushnil(L);
		while (lua_next(L, -2) != 0)
		{
			if (!lua_istable(L, -1))
			{
				lua_pop(L, 1);
				continue;
			}

			if (lua_getfield(L, -1, "src") != LUA_TSTRING)
			{
				lua_pop(L, 2); // "src" and inputs table item
				continue;
			}
			copyString(m_source, LuaWrapper::toType<const char*>(L, -1));
			lua_pop(L, 1); // "src"

			int meshes_count = m_meshes.size();
			if (!checkSource())
			{
				lua_pop(L, 1); // inputs table item
				g_log_error.log("Editor") << "Could not import \"" << m_source << "\"";
				continue;
			}
			if (m_is_importing) checkTask(true);

			if (lua_getfield(L, -1, "lod") == LUA_TNUMBER)
			{
				int lod = LuaWrapper::toType<int>(L, -1);
				for (int i = meshes_count; i < m_meshes.size(); ++i)
				{
					m_meshes[i].lod = lod;
				}
			}
			lua_pop(L, 1); // "lod"

			if (lua_getfield(L, -1, "materials") == LUA_TTABLE)
			{
				lua_pushnil(L);
				while (lua_next(L, -2) != 0) // for each material
				{
					if (lua_istable(L, -1))
					{
						auto* scene = m_importers.back().GetScene();
						ImportMaterial* material = getMatchingMaterial(
							L, &m_materials[m_materials.size() - scene->mNumMaterials], scene->mNumMaterials);
						if (!material)
						{
							g_log_error.log("Editor") << "No matching material found";
							lua_pop(L, 1); // materials table item
							continue;
						}
						
						if (lua_getfield(L, -1, "import") == LUA_TBOOLEAN)
						{
							material->import = LuaWrapper::toType<bool>(L, -1);
						}
						lua_pop(L, 1); // "import"

						if (lua_getfield(L, -1, "shader") == LUA_TSTRING)
						{
							copyString(material->shader, LuaWrapper::toType<const char*>(L, -1));
						}
						lua_pop(L, 1); // "import"


						if (lua_getfield(L, -1, "alpha_cutout") == LUA_TBOOLEAN)
						{
							material->alpha_cutout = LuaWrapper::toType<bool>(L, -1);
						}
						lua_pop(L, 1); // "alpha_cutout"

						if (lua_getfield(L, -1, "textures") == LUA_TTABLE)
						{
							lua_pushnil(L);
							ImportTexture* texture = material->textures;
							while (lua_next(L, -2) != 0) // for each texture
							{
								if (lua_getfield(L, -1, "import") == LUA_TBOOLEAN)
								{
									texture->import = LuaWrapper::toType<bool>(L, -1);
								}
								lua_pop(L, 1); // "import"

								if (lua_getfield(L, -1, "to_dds") == LUA_TBOOLEAN)
								{
									texture->to_dds = LuaWrapper::toType<bool>(L, -1);
								}
								lua_pop(L, 1); // "to_dds"

								if (lua_getfield(L, -1, "src") == LUA_TSTRING)
								{
									copyString(texture->src, LuaWrapper::toType<const char*>(L, -1));
									texture->is_valid = PlatformInterface::fileExists(texture->src);
								}
								lua_pop(L, 1); // "src"

								++texture;
								lua_pop(L, 1); // textures table item

								if (texture - material->textures > material->texture_count) break;
							}
						}
						lua_pop(L, 1); // "textures"
					}

					lua_pop(L, 1); // materials table item
				}
			}
			lua_pop(L, 1); // "materials"

			lua_pop(L, 1); // inputs table item
		}
	}
	lua_pop(L, 1);
	if (m_importers.empty())
	{
		g_log_error.log("Editor") << "Nothing to import";
		return 0;
	}

	convert(false);
	if (m_is_converting) checkTask(true);

	if (m_model.create_billboard_lod)
	{
		PathBuilder mesh_path(m_output_dir, "/");
		mesh_path << m_mesh_output_filename << ".msh";

		if (m_texture_output_dir[0])
		{
			PathBuilder texture_path(m_texture_output_dir, m_mesh_output_filename, "_billboard.dds");
			createBillboard(*this, Path(mesh_path), Path(texture_path), TEXTURE_SIZE);
		}
		else
		{
			PathBuilder texture_path(m_output_dir, "/", m_mesh_output_filename, " _billboard.dds");
			createBillboard(*this, Path(mesh_path), Path(texture_path), TEXTURE_SIZE);
		}
	}

	return 0;
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
	m_is_importing = false;
	m_is_converting = false;
	m_is_importing_texture = false;
}


void ImportAssetDialog::onAction()
{
	m_is_opened = !m_is_opened;
}


void ImportAssetDialog::onWindowGUI()
{
	if (ImGui::BeginDock("Import Asset", &m_is_opened))
	{
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

		if (m_is_converting || m_is_importing || m_is_importing_texture)
		{
			if (ImGui::Button("Cancel"))
			{
				if (m_is_importing_texture)
				{
					m_dds_convert_callback.cancel_requested = true;
				}
				else if (m_is_importing)
				{
					static_cast<ImportTask*>(m_task)->m_progress_handler.cancel_requested = true;
				}
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

		if (m_is_importing || m_is_converting)
		{
			ImGui::EndDock();
			return;
		}

		if (ImGui::Button("Add source"))
		{
			PlatformInterface::getOpenFilename(m_source, sizeof(m_source), "All\0*.*\0", m_source);
			checkSource();
			if (m_is_importing || m_is_converting)
			{
				ImGui::EndDock();
				return;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear all sources"))
		{
			m_importers.clear();
			m_animations.clear();
			m_materials.clear();
			m_meshes.clear();
			m_mesh_output_filename[0] = '\0';
		}

		onImageGUI();

		if (!m_importers.empty())
		{
			if (ImGui::CollapsingHeader("Advanced"))
			{
				//ImGui::Checkbox("Create billboard LOD", &m_create_billboard_lod);
				if (ImGui::Checkbox("Optimize meshes", &m_model.optimize_mesh_on_import)) checkSource();
				if (ImGui::Checkbox("Smooth normals", &m_model.gen_smooth_normal)) checkSource();

				if (m_is_importing || m_is_converting)
				{
					ImGui::EndDock();
					return;
				}

				ImGui::Checkbox("Remove doubles", &m_model.remove_doubles);
				ImGui::DragFloat("Scale", &m_model.mesh_scale, 0.01f, 0.001f, 0);
				ImGui::Combo("Orientation", &(int&)m_model.orientation, "Y up\0Z up\0-Z up\0-X up\0");
				ImGui::Checkbox("Make physics convex", &m_model.make_convex);
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
				for (auto& mat : m_materials)
				{
					for (int i = 0; i < mat.texture_count; ++i)
					{
						if (mat.textures[i].is_valid || !mat.textures[i].import) continue;
						ImGui::Text("Texture %s is not valid", mat.textures[i].path);
					}
				}
				if (ImGui::Button("OK"))
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
	}
	ImGui::EndDock();
}
