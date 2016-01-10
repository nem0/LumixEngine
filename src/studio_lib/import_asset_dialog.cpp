#include "import_asset_dialog.h"
#include "assimp/postprocess.h"
#include "assimp/ProgressHandler.hpp"
#include "assimp/scene.h"
#include "core/crc32.h"
#include "core/FS/ifile.h"
#include "core/FS/file_system.h"
#include "core/FS/os_file.h"
#include "core/log.h"
#include "core/MT/task.h"
#include "core/path_utils.h"
#include "core/system.h"
#include "crnlib.h"
#include "debug/floating_points.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "metadata.h"
#include "imgui/imgui.h"
#include "platform_interface.h"
#include "physics/physics_geometry_manager.h"
#include "renderer/model.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "utils.h"


typedef StringBuilder<Lumix::MAX_PATH_LENGTH> PathBuilder;


enum class VertexAttributeDef : Lumix::uint32
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


struct DDSConvertCallbackData
{
	ImportAssetDialog* dialog;
	const char* dest_path;
};


crn_bool ddsConvertCallback(crn_uint32 phase_index,
	crn_uint32 total_phases,
	crn_uint32 subphase_index,
	crn_uint32 total_subphases,
	void* pUser_data_ptr)
{
	DDSConvertCallbackData* data = (DDSConvertCallbackData*)pUser_data_ptr;

	float fraction = phase_index / float(total_phases) +
					 (subphase_index / float(total_subphases)) / total_phases;
	data->dialog->setImportMessage(
		StringBuilder<Lumix::MAX_PATH_LENGTH + 50>("Saving ") << data->dest_path << "\n"
															  << int(fraction * 100)
															  << "%%");

	return true;
}


static bool saveAsRaw(ImportAssetDialog& dialog,
	Lumix::FS::FileSystem& fs,
	const Lumix::uint8* image_data,
	int image_width,
	int image_height,
	const char* dest_path,
	float scale,
	Lumix::IAllocator& allocator)
{
	ASSERT(image_data);

	dialog.setImportMessage(StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Saving ") << dest_path);

	auto* file = fs.open(fs.getDiskDevice(),
		Lumix::Path(dest_path),
		Lumix::FS::Mode::WRITE | Lumix::FS::Mode::CREATE);
	if (!file)
	{
		dialog.setMessage(
			StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Could not save ") << dest_path);
		return false;
	}

	Lumix::Array<Lumix::uint16> data(allocator);
	data.resize(image_width * image_height);
	for (int j = 0; j < image_height; ++j)
	{
		for (int i = 0; i < image_width; ++i)
		{
			data[i + j * image_width] =
				Lumix::uint16(scale * image_data[(i + j * image_width) * 4]);
		}
	}

	file->write((const char*)&data[0], data.size() * sizeof(data[0]));
	fs.close(*file);
	return true;
}


static bool saveAsDDS(ImportAssetDialog& dialog,
	Lumix::FS::FileSystem& fs,
	const char* source_path,
	const Lumix::uint8* image_data,
	int image_width,
	int image_height,
	const char* dest_path)
{
	ASSERT(image_data);

	dialog.setImportMessage(StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Saving ") << dest_path);

	DDSConvertCallbackData callback_data;
	callback_data.dialog = &dialog;
	callback_data.dest_path = dest_path;

	crn_uint32 size;
	crn_comp_params comp_params;
	comp_params.m_width = image_width;
	comp_params.m_height = image_height;
	comp_params.m_file_type = cCRNFileTypeDDS;
	comp_params.m_format = cCRNFmtDXT3;
	comp_params.m_quality_level = cCRNMinQualityLevel;
	comp_params.m_dxt_quality = cCRNDXTQualitySuperFast;
	comp_params.m_dxt_compressor_type = cCRNDXTCompressorRYG;
	comp_params.m_pProgress_func = ddsConvertCallback;
	comp_params.m_pProgress_func_data = &callback_data;
	comp_params.m_num_helper_threads = 3;
	comp_params.m_pImages[0][0] = (Lumix::uint32*)image_data;
	crn_mipmap_params mipmap_params;
	mipmap_params.m_mode = cCRNMipModeGenerateMips;

	void* data = crn_compress(comp_params, mipmap_params, size);
	if (!data)
	{
		dialog.setMessage(
			StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Could not convert ") << source_path);
		return false;
	}

	auto* file = fs.open(fs.getDiskDevice(),
		Lumix::Path(dest_path),
		Lumix::FS::Mode::WRITE | Lumix::FS::Mode::CREATE);
	if (!file)
	{
		dialog.setMessage(
			StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Could not save ") << dest_path);
		crn_free_block(data);
		return false;
	}

	file->write((const char*)data, size);
	fs.close(*file);
	crn_free_block(data);
	return true;
}


struct ImportTextureTask : public Lumix::MT::Task
{
	ImportTextureTask(ImportAssetDialog& dialog)
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
		char basename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getBasename(basename, sizeof(basename), source);

		if (to_dds)
		{
			PathBuilder dest_path(output_dir);
			dest_path << "/" << basename << ".dds";
			Lumix::copyString(out, max_size, dest_path);
			return;
		}

		if (to_raw)
		{
			PathBuilder dest_path(output_dir);
			dest_path << "/" << basename << ".raw";
			Lumix::copyString(out, max_size, dest_path);
			return;
		}

		char ext[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getExtension(ext, sizeof(ext), source);
		PathBuilder dest_path(output_dir);
		dest_path << "/" << basename << "." << ext;
		Lumix::copyString(out, max_size, dest_path);
	}


	int task() override
	{
		m_dialog.setImportMessage("Importing texture...");
		int image_width;
		int image_height;
		int image_comp;
		auto* data = stbi_load(m_dialog.m_source, &image_width, &image_height, &image_comp, 4);

		if (!data)
		{
			m_dialog.setMessage(
				StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Could not load ") << m_dialog.m_source);
			return -1;
		}

		char dest_path[Lumix::MAX_PATH_LENGTH];
		getDestinationPath(m_dialog.m_output_dir,
			m_dialog.m_source,
			m_dialog.m_convert_to_dds,
			m_dialog.m_convert_to_raw,
			dest_path,
			Lumix::lengthOf(dest_path));

		if (m_dialog.m_convert_to_dds)
		{
			m_dialog.setImportMessage("Converting to DDS...");

			saveAsDDS(m_dialog,
				m_dialog.m_editor.getEngine().getFileSystem(),
				m_dialog.m_source,
				data,
				image_width,
				image_height,
				dest_path);
		}
		else if (m_dialog.m_convert_to_raw)
		{
			m_dialog.setImportMessage("Converting to RAW...");

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
			m_dialog.setImportMessage("Copying...");

			if (!Lumix::copyFile(m_dialog.m_source, dest_path))
			{
				m_dialog.setMessage(
					StringBuilder<Lumix::MAX_PATH_LENGTH * 2 + 30>("Could not copy ")
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


struct ImportTask : public Lumix::MT::Task
{
	struct ProgressHandler : public Assimp::ProgressHandler
	{
		bool Update(float percentage) override
		{
			m_task->m_dialog.setImportMessage(
				StringBuilder<50>("Importing... ") << int(percentage * 100) << "%%");

			return true;
		}

		ImportTask* m_task;
	};

	ImportTask(ImportAssetDialog& dialog)
		: Task(dialog.m_editor.getAllocator())
		, m_dialog(dialog)
	{
		m_dialog.m_importer.SetProgressHandler(&m_progress_handler);
	}


	~ImportTask() { m_dialog.m_importer.SetProgressHandler(nullptr); }


	int task() override
	{
		m_progress_handler.m_task = this;
		Lumix::enableFloatingPointTraps(false);
		m_dialog.m_importer.SetPropertyInteger(
			AI_CONFIG_PP_RVC_FLAGS, aiComponent_LIGHTS | aiComponent_CAMERAS);
		unsigned int flags = aiProcess_JoinIdenticalVertices | aiProcess_RemoveComponent |
							 aiProcess_GenUVCoords | aiProcess_RemoveRedundantMaterials |
							 aiProcess_Triangulate | aiProcess_LimitBoneWeights |
							 aiProcess_OptimizeGraph | aiProcess_CalcTangentSpace;
		flags |= m_dialog.m_gen_smooth_normal ? aiProcess_GenSmoothNormals : aiProcess_GenNormals;
		flags |= m_dialog.m_optimize_mesh_on_import ? aiProcess_OptimizeMeshes : 0;
		const aiScene* scene = m_dialog.m_importer.ReadFile(m_dialog.m_source, flags);
		if (!scene || !scene->mMeshes || !scene->mMeshes[0]->mTangents)
		{
			m_dialog.m_importer.FreeScene();
			m_dialog.setMessage(m_dialog.m_importer.GetErrorString());
			Lumix::g_log_error.log("import") << m_dialog.m_importer.GetErrorString();
		}
		else
		{
			m_dialog.m_mesh_mask.resize(scene->mNumMeshes);
			for (int i = 0; i < m_dialog.m_mesh_mask.size(); ++i)
			{
				m_dialog.m_mesh_mask[i] = true;
			}
		}

		Lumix::enableFloatingPointTraps(true);

		return 0;
	}


	ImportAssetDialog& m_dialog;
	ProgressHandler m_progress_handler;

}; // struct ImportTask


struct ConvertTask : public Lumix::MT::Task
{
	struct SkinInfo
	{
		SkinInfo()
		{
			memset(this, 0, sizeof(SkinInfo));
			index = 0;
		}

		float weights[4];
		Lumix::uint16 bone_indices[4];
		int index;
	};


	ConvertTask(ImportAssetDialog& dialog, float scale)
		: Task(dialog.m_editor.getAllocator())
		, m_dialog(dialog)
		, m_filtered_meshes(dialog.m_editor.getAllocator())
		, m_scale(scale)
	{
	}


	bool saveEmbeddedTextures(const aiScene* scene)
	{
		bool success = true;
		m_dialog.m_saved_embedded_textures.clear();
		for (unsigned int i = 0; i < scene->mNumTextures; ++i)
		{
			const aiTexture* texture = scene->mTextures[i];
			if (texture->mHeight != 0)
			{
				m_dialog.setMessage("Uncompressed texture embedded. This is not supported.");
				return false;
			}
			PathBuilder texture_name("texture");
			texture_name << i << ".dds";
			int width, height, comp;
			auto data = stbi_load_from_memory(
				(stbi_uc*)texture->pcData, texture->mWidth, &width, &height, &comp, 4);
			if (!data) continue;

			m_dialog.m_saved_embedded_textures.push(
				Lumix::string(texture_name, m_dialog.m_editor.getAllocator()));
			PathBuilder dest(m_dialog.m_texture_output_dir[0] ? m_dialog.m_texture_output_dir
															  : m_dialog.m_output_dir);
			dest << "/" << texture_name;
			bool saved = saveAsDDS(m_dialog,
				m_dialog.m_editor.getEngine().getFileSystem(),
				"Embedded texture",
				data,
				width,
				height,
				dest);
			success = success && saved;

			stbi_image_free(data);
		}
		return success;
	}


	bool saveTexture(const char* texture_path,
		const char* source_mesh_dir,
		Lumix::FS::IFile& material_file) const
	{
		Lumix::string texture_source_path(texture_path, m_dialog.m_editor.getAllocator());
		int mapping_index = m_dialog.m_path_mapping.find(texture_source_path);
		if (mapping_index >= 0)
		{
			texture_source_path = m_dialog.m_path_mapping.at(mapping_index);
		}
		bool is_embedded = false;
		if (texture_source_path[0] == '*')
		{
			is_embedded = true;
			int index;
			Lumix::fromCString(
				texture_source_path.c_str() + 1, texture_source_path.length() - 1, &index);
			texture_source_path = m_dialog.m_saved_embedded_textures[index];
		}

		Lumix::PathUtils::FileInfo texture_info(texture_source_path.c_str());
		if (!m_dialog.m_texture_output_dir[0])
		{
			material_file << "\t, \"texture\" : {\n\t\t\"source\" : \"" << texture_info.m_basename
						  << ".";
			material_file << (m_dialog.m_convert_to_dds ? "dds" : texture_info.m_extension);
			material_file << "\"\n }\n";
		}
		else
		{
			material_file << "\t, \"texture\" : {\n\t\t\"source\" : \"";
			char from_root_path[Lumix::MAX_PATH_LENGTH];
			m_dialog.m_editor.getRelativePath(
				from_root_path, Lumix::lengthOf(from_root_path), m_dialog.m_texture_output_dir);
			material_file << "/" << from_root_path;
			material_file << texture_info.m_basename << ".";
			material_file << (m_dialog.m_convert_to_dds ? "dds" : texture_info.m_extension);
			material_file << "\"\n }\n";
		}

		bool is_already_saved = m_dialog.m_saved_textures.indexOf(texture_source_path) >= 0;
		if (is_embedded || is_already_saved) return true;

		PathBuilder source_absolute(source_mesh_dir);
		source_absolute << "/" << texture_source_path.c_str();
		const char* source = Lumix::PathUtils::isAbsolute(texture_source_path.c_str())
								 ? texture_source_path.c_str()
								 : source_absolute;

		if (m_dialog.m_convert_to_dds && Lumix::compareString(texture_info.m_extension, "dds") != 0)
		{
			PathBuilder dest(m_dialog.m_texture_output_dir[0] ? m_dialog.m_texture_output_dir
															  : m_dialog.m_output_dir);
			dest << "/" << texture_info.m_basename << ".dds";
			int image_width, image_height, dummy;
			auto data = stbi_load(source, &image_width, &image_height, &dummy, 4);
			if (!data)
			{
				StringBuilder<Lumix::MAX_PATH_LENGTH + 20> error_msg(
					"Could not load image ", source);
				m_dialog.setMessage(error_msg);
				return false;
			}

			if (!saveAsDDS(m_dialog,
					m_dialog.m_editor.getEngine().getFileSystem(),
					source,
					data,
					image_width,
					image_height,
					dest))
			{
				stbi_image_free(data);
				m_dialog.setMessage(StringBuilder<Lumix::MAX_PATH_LENGTH * 2 + 20>(
					"Error converting ", source, " to ", dest));
				return false;
			}
			stbi_image_free(data);
		}
		else
		{
			PathBuilder dest(m_dialog.m_output_dir);
			dest << "/" << texture_info.m_basename << "." << texture_info.m_extension;
			if (Lumix::compareString(source, dest) != 0 && !Lumix::copyFile(source, dest))
			{
				m_dialog.setMessage(StringBuilder<Lumix::MAX_PATH_LENGTH * 2 + 20>(
					"Error copying ", source, " to ", dest));
				return false;
			}
		}

		m_dialog.m_saved_textures.push(texture_source_path);
		return true;
	}


	bool saveLumixMaterials()
	{
		if (!m_dialog.m_import_materials) return true;

		m_dialog.setImportMessage("Importing materials...");
		const aiScene* scene = m_dialog.m_importer.GetScene();

		if (!saveEmbeddedTextures(scene))
		{
			m_dialog.setMessage("Failed to import embedded texture");
		}

		m_dialog.m_saved_textures.clear();

		int undefined_count = 0;
		char source_mesh_dir[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getDir(source_mesh_dir, sizeof(source_mesh_dir), m_dialog.m_source);

		for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
		{
			const aiMaterial* material = scene->mMaterials[i];
			if (!saveMaterial(material, source_mesh_dir, &undefined_count))
			{
				return false;
			}
		}
		return true;
	}


	bool saveMaterial(const aiMaterial* material,
		const char* source_mesh_dir,
		int* undefined_count) const
	{
		ASSERT(undefined_count);

		aiString material_name;
		material->Get(AI_MATKEY_NAME, material_name);
		PathBuilder output_material_name(m_dialog.m_output_dir);
		output_material_name << "/" << material_name.C_Str() << ".mat";

		m_dialog.setImportMessage(
			StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Converting ") << output_material_name);
		auto& fs = m_dialog.m_editor.getEngine().getFileSystem();
		auto* file = fs.open(fs.getDiskDevice(),
			Lumix::Path(output_material_name),
			Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
		if (!file)
		{
			m_dialog.setMessage(StringBuilder<20 + Lumix::MAX_PATH_LENGTH>(
				"Could not create ", output_material_name));
			return false;
		}

		const aiScene* scene = m_dialog.m_importer.GetScene();
		*file << "{\n\t\"shader\" : \"shaders/"
			  << (isSkinned(scene, material) ? "skinned" : "rigid") << ".shd\"\n";

		if (material->GetTextureCount(aiTextureType_DIFFUSE) == 1)
		{
			aiString texture_path;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &texture_path);
			saveTexture(texture_path.C_Str(), source_mesh_dir, *file);
		}
		else
		{
			saveTexture(
				PathBuilder("undefined") << *undefined_count << ".dds", source_mesh_dir, *file);
			++*undefined_count;
		}

		if (material->GetTextureCount(aiTextureType_NORMALS) == 1)
		{
			aiString texture_path;
			material->GetTexture(aiTextureType_NORMALS, 0, &texture_path);
			saveTexture(texture_path.C_Str(), source_mesh_dir, *file);
		}
		else if (material->GetTextureCount(aiTextureType_HEIGHT) == 1)
		{
			aiString texture_path;
			material->GetTexture(aiTextureType_HEIGHT, 0, &texture_path);
			saveTexture(texture_path.C_Str(), source_mesh_dir, *file);
		}
		else if (material->GetTextureCount(aiTextureType_NORMALS) > 1)
		{
			m_dialog.setMessage(StringBuilder<Lumix::MAX_PATH_LENGTH + 20>(
				"Too many normal maps in ", material_name.C_Str()));
			fs.close(*file);
			return false;
		}
		file->write("}", 1);
		fs.close(*file);
		return true;
	}


	int task() override
	{
		if (saveLumixPhysics() && saveLumixModel() && saveLumixMaterials())
		{
			m_dialog.setMessage("Success.");
		}
		return 0;
	}


	static int countNodes(const aiNode* node)
	{
		int count = 1;
		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			count += countNodes(node->mChildren[i]);
		}
		return count;
	}


	static bool isSkinned(const aiMesh* mesh) { return mesh->mNumBones > 0; }


	static bool isSkinned(const aiScene* scene, const aiMaterial* material)
	{
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
		{
			if (scene->mMaterials[scene->mMeshes[i]->mMaterialIndex] == material &&
				isSkinned(scene->mMeshes[i]))
			{
				return true;
			}
		}
		return false;
	}


	static void getBoneNamesHashes(const aiNode* node, Lumix::Array<int>& node_names)
	{
		node_names.push(Lumix::crc32(node->mName.C_Str()));
		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			getBoneNamesHashes(node->mChildren[i], node_names);
		}
	}


	void fillSkinInfo(const aiScene* scene, Lumix::Array<SkinInfo>& infos, int vertices_count) const
	{

		Lumix::Array<int> node_names(m_dialog.m_editor.getAllocator());
		getBoneNamesHashes(scene->mRootNode, node_names);
		infos.resize(vertices_count);

		int offset = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			for (unsigned int j = 0; j < mesh->mNumBones; ++j)
			{
				const aiBone* bone = mesh->mBones[j];
				int bone_index = node_names.indexOf(Lumix::crc32(bone->mName.C_Str()));
				for (unsigned int k = 0; k < bone->mNumWeights; ++k)
				{
					auto& info = infos[offset + bone->mWeights[k].mVertexId];
					info.weights[info.index] = bone->mWeights[k].mWeight;
					info.bone_indices[info.index] = bone_index;
					++info.index;
				}
			}
			offset += mesh->mNumVertices;
		}
	}


	static Lumix::uint32 packuint32(Lumix::uint8 _x,
		Lumix::uint8 _y,
		Lumix::uint8 _z,
		Lumix::uint8 _w)
	{
		union
		{
			Lumix::uint32 ui32;
			Lumix::uint8 arr[4];
		} un;

		un.arr[0] = _x;
		un.arr[1] = _y;
		un.arr[2] = _z;
		un.arr[3] = _w;

		return un.ui32;
	}


	static Lumix::uint32 packF4u(const aiVector3D& vec)
	{
		const Lumix::uint8 xx = Lumix::uint8(vec.x * 127.0f + 128.0f);
		const Lumix::uint8 yy = Lumix::uint8(vec.y * 127.0f + 128.0f);
		const Lumix::uint8 zz = Lumix::uint8(vec.z * 127.0f + 128.0f);
		const Lumix::uint8 ww = Lumix::uint8(0);
		return packuint32(xx, yy, zz, ww);
	}


	void writeGeometry(Lumix::FS::IFile& file) const
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		Lumix::int32 indices_count = 0;
		int vertices_count = 0;
		Lumix::int32 vertices_size = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			indices_count += mesh->mNumFaces * 3;
			vertices_count += mesh->mNumVertices;
			vertices_size += mesh->mNumVertices * getVertexSize(mesh);
		}

		file.write((const char*)&indices_count, sizeof(indices_count));
		Lumix::int32 polygon_idx = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			for (unsigned int j = 0; j < mesh->mNumFaces; ++j)
			{
				polygon_idx = mesh->mFaces[j].mIndices[0];
				file.write((const char*)&polygon_idx, sizeof(polygon_idx));
				polygon_idx = mesh->mFaces[j].mIndices[1];
				file.write((const char*)&polygon_idx, sizeof(polygon_idx));
				polygon_idx = mesh->mFaces[j].mIndices[2];
				file.write((const char*)&polygon_idx, sizeof(polygon_idx));
			}
		}

		file.write((const char*)&vertices_size, sizeof(vertices_size));

		Lumix::Array<SkinInfo> skin_infos(m_dialog.m_editor.getAllocator());
		fillSkinInfo(scene, skin_infos, vertices_count);

		int skin_index = 0;
		aiMatrix3x3 normal_matrix(scene->mRootNode->mTransformation);

		for (auto* mesh : m_filtered_meshes)
		{
			bool is_skinned = isSkinned(mesh);
			for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
			{
				if (is_skinned)
				{
					file.write((const char*)skin_infos[skin_index].weights,
						sizeof(skin_infos[skin_index].weights));
					file.write((const char*)skin_infos[skin_index].bone_indices,
						sizeof(skin_infos[skin_index].bone_indices));
				}
				++skin_index;

				auto v = scene->mRootNode->mTransformation * mesh->mVertices[j];

				Lumix::Vec3 position(v.x, v.y, v.z);
				position *= m_scale;
				file.write((const char*)&position, sizeof(position));

				if (mesh->mColors[0])
				{
					auto assimp_color = mesh->mColors[0][j];
					Lumix::uint8 color[4];
					color[0] = Lumix::uint8(assimp_color.r * 255);
					color[1] = Lumix::uint8(assimp_color.g * 255);
					color[2] = Lumix::uint8(assimp_color.b * 255);
					color[3] = Lumix::uint8(assimp_color.a * 255);
					file.write(color, sizeof(color));
				}

				auto normal = normal_matrix * mesh->mNormals[j];
				Lumix::uint32 int_normal = packF4u(normal);
				file.write((const char*)&int_normal, sizeof(int_normal));

				if (mesh->mTangents)
				{
					auto tangent = mesh->mTangents[j];
					Lumix::uint32 int_tangent = packF4u(tangent);
					file.write((const char*)&int_tangent, sizeof(int_tangent));
				}

				auto uv = mesh->mTextureCoords[0][j];
				uv.y = -uv.y;
				file.write((const char*)&uv, sizeof(uv.x) + sizeof(uv.y));
			}
		}
	}


	static int getAttributeCount(const aiMesh* mesh)
	{
		int count = 3; // position, normal, uv
		if (isSkinned(mesh)) count += 2;
		if (mesh->mColors[0]) ++count;
		if (mesh->mTangents) ++count;
		return count;
	}


	static int getVertexSize(const aiMesh* mesh)
	{
		static const int POSITION_SIZE = sizeof(float) * 3;
		static const int NORMAL_SIZE = sizeof(Lumix::uint8) * 4;
		static const int TANGENT_SIZE = sizeof(Lumix::uint8) * 4;
		static const int UV_SIZE = sizeof(float) * 2;
		static const int COLOR_SIZE = sizeof(Lumix::uint8) * 4;
		static const int BONE_INDICES_WEIGHTS_SIZE = sizeof(float) * 4 + sizeof(Lumix::uint16) * 4;
		int size = POSITION_SIZE + NORMAL_SIZE + UV_SIZE;
		if (mesh->mTangents) size += TANGENT_SIZE;
		if (mesh->mColors[0]) size += COLOR_SIZE;
		if (isSkinned(mesh)) size += BONE_INDICES_WEIGHTS_SIZE;
		return size;
	}


	static const aiNode* getOwner(const aiNode* node, int mesh_index)
	{
		for (int i = 0; i < (int)node->mNumMeshes; ++i)
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


	const aiNode* getOwner(const aiMesh* mesh) const
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		for (int i = 0; i < int(scene->mNumMeshes); ++i)
		{
			if (scene->mMeshes[i] == mesh) return getOwner(scene->mRootNode, i);
		}
		return nullptr;
	}


	aiString getMeshName(const aiMesh* mesh) const
	{
		aiString mesh_name = mesh->mName;
		int length = Lumix::stringLength(mesh_name.C_Str());
		if (length == 0)
		{
			const auto* node = getOwner(mesh);
			if (node)
			{
				mesh_name = node->mName;
			}
		}
		return mesh_name;
	}


	void writeMeshes(Lumix::FS::IFile& file) const
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		Lumix::int32 mesh_count = 0;
		for (int i = 0; i < m_dialog.m_mesh_mask.size(); ++i)
		{
			if (m_dialog.m_mesh_mask[i]) ++mesh_count;
		}

		file.write((const char*)&mesh_count, sizeof(mesh_count));
		Lumix::int32 attribute_array_offset = 0;
		Lumix::int32 indices_offset = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			int vertex_size = getVertexSize(mesh);
			aiString material_name;
			scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, material_name);
			Lumix::int32 length = Lumix::stringLength(material_name.C_Str());
			file.write((const char*)&length, sizeof(length));
			file.write((const char*)material_name.C_Str(), length);

			file.write((const char*)&attribute_array_offset, sizeof(attribute_array_offset));
			Lumix::int32 attribute_array_size = mesh->mNumVertices * vertex_size;
			attribute_array_offset += attribute_array_size;
			file.write((const char*)&attribute_array_size, sizeof(attribute_array_size));

			file.write((const char*)&indices_offset, sizeof(indices_offset));
			Lumix::int32 mesh_tri_count = mesh->mNumFaces;
			indices_offset += mesh->mNumFaces * 3;
			file.write((const char*)&mesh_tri_count, sizeof(mesh_tri_count));

			aiString mesh_name = getMeshName(mesh);
			length = Lumix::stringLength(mesh_name.C_Str());

			file.write((const char*)&length, sizeof(length));
			file.write((const char*)mesh_name.C_Str(), length);

			Lumix::int32 attribute_count = getAttributeCount(mesh);
			file.write((const char*)&attribute_count, sizeof(attribute_count));

			if (isSkinned(mesh))
			{
				writeAttribute("in_weights", VertexAttributeDef::FLOAT4, file);
				writeAttribute("in_indices", VertexAttributeDef::SHORT4, file);
			}

			writeAttribute("in_position", VertexAttributeDef::POSITION, file);
			if (mesh->mColors[0]) writeAttribute("in_colors", VertexAttributeDef::BYTE4, file);
			writeAttribute("in_normal", VertexAttributeDef::BYTE4, file);
			if (mesh->mTangents) writeAttribute("in_tangents", VertexAttributeDef::BYTE4, file);
			writeAttribute("in_tex_coords", VertexAttributeDef::FLOAT2, file);
		}
	}


	static void writeAttribute(const char* attribute_name,
		VertexAttributeDef attribute_type,
		Lumix::FS::IFile& file)
	{
		Lumix::uint32 length = Lumix::stringLength(attribute_name);
		file.write((const char*)&length, sizeof(length));
		file.write(attribute_name, length);

		Lumix::uint32 type = (Lumix::uint32)attribute_type;
		file.write((const char*)&type, sizeof(type));
	}


	static void writeNode(Lumix::FS::IFile& file, const aiNode* node, aiMatrix4x4 parent_transform)
	{
		Lumix::int32 len = Lumix::stringLength(node->mName.C_Str());
		file.write((const char*)&len, sizeof(len));
		file.write(node->mName.C_Str(), node->mName.length + 1);

		if (node->mParent)
		{
			Lumix::int32 len = Lumix::stringLength(node->mParent->mName.C_Str());
			file.write((const char*)&len, sizeof(len));
			file.write(node->mParent->mName.C_Str(), node->mParent->mName.length);
		}
		else
		{
			Lumix::int32 len = 0;
			file.write((const char*)&len, sizeof(len));
		}

		aiQuaterniont<float> rot;
		aiVector3t<float> pos;
		(parent_transform * node->mTransformation).DecomposeNoScaling(rot, pos);
		file.write((const char*)&pos, sizeof(pos));
		file.write((const char*)&rot.x, sizeof(rot.x));
		file.write((const char*)&rot.y, sizeof(rot.y));
		file.write((const char*)&rot.z, sizeof(rot.z));
		file.write((const char*)&rot.w, sizeof(rot.w));

		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			writeNode(file, node->mChildren[i], parent_transform * node->mTransformation);
		}
	}


	void writeLods(Lumix::FS::IFile& file) const
	{
		Lumix::int32 lods[] = {-1, -1, -1, -1, -1, -1, -1, -1};
		Lumix::int32 lod_count = -1;
		float factors[8];
		for (int i = 0; i < m_filtered_meshes.size(); ++i)
		{
			int lod = getMeshLOD(&m_filtered_meshes[i]);
			if (lod < 0 || lod >= Lumix::lengthOf(lods)) break;
			lods[lod] = i;
			factors[lod] = getMeshLODFactor(m_filtered_meshes[i]);
			lod_count = Lumix::Math::maxValue(lod_count, lod + 1);
		}

		if (lods[0] < 0)
		{
			lod_count = 1;
			file.write((const char*)&lod_count, sizeof(lod_count));
			Lumix::int32 to_mesh = m_filtered_meshes.size() - 1;
			file.write((const char*)&to_mesh, sizeof(to_mesh));
			float distance = FLT_MAX;
			file.write((const char*)&distance, sizeof(distance));
		}
		else
		{
			file.write((const char*)&lod_count, sizeof(lod_count));
			for (int i = 0; i < lod_count; ++i)
			{
				Lumix::int32 to_mesh = lods[i];
				file.write((const char*)&to_mesh, sizeof(to_mesh));
				float factor = i == lod_count - 1 ? FLT_MAX : factors[i];
				file.write((const char*)&factor, sizeof(factor));
			}
		}
	}


	void writeSkeleton(Lumix::FS::IFile& file) const
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		Lumix::int32 count = countNodes(scene->mRootNode);

		if (count == 1)
		{
			count = 0;
		}
		file.write((const char*)&count, sizeof(count));
		if (count > 0)
		{
			writeNode(file, scene->mRootNode, aiMatrix4x4());
		}
	}


	void writePhysicsHeader(Lumix::FS::IFile& file) const
	{
		Lumix::PhysicsGeometry::Header header;
		header.m_magic = Lumix::PhysicsGeometry::HEADER_MAGIC;
		header.m_version = (Lumix::uint32)Lumix::PhysicsGeometry::Versions::LAST;
		header.m_convex = (Lumix::uint32)m_dialog.m_make_convex;
		file.write((const char*)&header, sizeof(header));
	}


	bool saveLumixPhysics()
	{
		if (!m_dialog.m_import_physics) return true;

		m_dialog.setImportMessage("Importing physics...");
		char filename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getBasename(filename, sizeof(filename), m_dialog.m_source);
		Lumix::catString(filename, ".phy");
		auto& fs = m_dialog.m_editor.getEngine().getFileSystem();
		PathBuilder phy_path(m_dialog.m_output_dir);
		phy_path << "/" << filename;
		Lumix::FS::IFile* file = fs.open(fs.getDiskDevice(),
			Lumix::Path(phy_path),
			Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
		if (!file)
		{
			Lumix::g_log_error.log("import") << "Could not create file " << phy_path;
			return false;
		}

		writePhysicsHeader(*file);
		Lumix::int32 count = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			count += (Lumix::int32)mesh->mNumVertices;
		}
		file->write((const char*)&count, sizeof(count));
		for (auto* mesh : m_filtered_meshes)
		{
			file->write(
				(const char*)mesh->mVertices, sizeof(mesh->mVertices[0]) * mesh->mNumVertices);
		}

		if (!m_dialog.m_make_convex) writePhysiscTriMesh(*file);
		fs.close(*file);

		return true;
	}


	void writePhysiscTriMesh(Lumix::FS::IFile& file)
	{
		int count = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			count += (int)mesh->mNumFaces * 3;
		}
		file.write((const char*)&count, sizeof(count));
		int offset = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			for (unsigned int j = 0; j < mesh->mNumFaces; ++j)
			{
				ASSERT(mesh->mFaces[j].mNumIndices == 3);
				Lumix::uint32 index = mesh->mFaces[j].mIndices[0] + offset;
				file.write((const char*)&index, sizeof(index));
				index = mesh->mFaces[j].mIndices[1] + offset;
				file.write((const char*)&index, sizeof(index));
				index = mesh->mFaces[j].mIndices[2] + offset;
				file.write((const char*)&index, sizeof(index));
			}
			offset += mesh->mNumVertices;
		}
	}


	bool checkModel() const
	{
		for (auto* mesh : m_filtered_meshes)
		{
			if (!mesh->HasNormals())
			{
				m_dialog.setMessage(
					StringBuilder<256>("Mesh ", getMeshName(mesh).C_Str(), " has no normals."));
				return false;
			}
			if (!mesh->HasPositions())
			{
				m_dialog.setMessage(
					StringBuilder<256>("Mesh ", getMeshName(mesh).C_Str(), " has no positions."));
				return false;
			}
			if (!mesh->HasTextureCoords(0))
			{
				m_dialog.setMessage(StringBuilder<256>(
					"Mesh ", getMeshName(mesh).C_Str(), " has no texture coords."));
				return false;
			}
		}
		return true;
	}


	static void writeModelHeader(Lumix::FS::IFile& file)
	{
		Lumix::Model::FileHeader header;
		header.m_magic = Lumix::Model::FILE_MAGIC;
		header.m_version = (Lumix::uint32)Lumix::Model::FileVersion::LATEST;
		file.write((const char*)&header, sizeof(header));
	}


	float getMeshLODFactor(const aiMesh* mesh) const
	{
		const char* mesh_name = getMeshName(mesh).C_Str();
		int len = Lumix::stringLength(mesh_name);
		if (len < 5) return FLT_MAX;

		const char* last = mesh_name + len - 1;
		while (last > mesh_name && *last >= '0' && *last <= '9')
		{
			--last;
		}
		++last;
		if (last < mesh_name + 4) FLT_MAX;
		if (Lumix::compareStringN(last - 4, "_LOD", 4) != 0) return FLT_MAX;
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
		Lumix::fromCString(begin_factor, int(end_of_factor - begin_factor), &factor);

		return float(factor);
	}


	int getMeshLOD(const aiMesh* const* mesh_ptr) const
	{
		const aiMesh* const mesh = *mesh_ptr;

		const char* mesh_name = getMeshName(mesh).C_Str();
		int len = Lumix::stringLength(mesh_name);
		if (len < 5) return -1;

		const char* last = mesh_name + len - 1;
		while (last > mesh_name && *last >= '0' && *last <= '9')
		{
			--last;
		}
		++last;
		if (last < mesh_name + 4) return -1;
		if (Lumix::compareStringN(last - 4, "_LOD", 4) != 0) return -1;

		int lod;
		Lumix::fromCString(last, len - int(last - mesh_name), &lod);

		return lod;
	}


	void filterMeshes()
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		m_filtered_meshes.clear();
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
		{
			if (m_dialog.m_mesh_mask[i]) m_filtered_meshes.push(scene->mMeshes[i]);
		}

		static ConvertTask* that = this;
		auto cmpMeshes = [](const void* a, const void* b) -> int
		{
			auto a_mesh = static_cast<aiMesh* const*>(a);
			auto b_mesh = static_cast<aiMesh* const*>(b);
			return that->getMeshLOD(a_mesh) - that->getMeshLOD(b_mesh);
		};

		qsort(&m_filtered_meshes[0],
			m_filtered_meshes.size(),
			sizeof(m_filtered_meshes[0]),
			cmpMeshes);
	}


	bool saveLumixModel()
	{
		ASSERT(m_dialog.m_output_dir[0] != '\0');
		if (!m_dialog.m_import_model) return true;
		if (!checkModel()) return false;

		m_dialog.setImportMessage("Importing model...");
		PlatformInterface::makePath(m_dialog.m_output_dir);
		if (m_dialog.m_texture_output_dir[0])
			PlatformInterface::makePath(m_dialog.m_texture_output_dir);

		char basename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getBasename(basename, sizeof(basename), m_dialog.m_source);
		PathBuilder path(m_dialog.m_output_dir);
		path << "/" << basename << ".msh";

		auto& fs = m_dialog.m_editor.getEngine().getFileSystem();
		Lumix::FS::IFile* file = fs.open(fs.getDiskDevice(),
			Lumix::Path(path),
			Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
		if (!file)
		{
			m_dialog.setMessage(
				StringBuilder<Lumix::MAX_PATH_LENGTH + 15>("Failed to open ", path));
			return false;
		}

		filterMeshes();

		writeModelHeader(*file);
		writeMeshes(*file);
		writeGeometry(*file);
		writeSkeleton(*file);
		writeLods(*file);

		fs.close(*file);
		return true;
	}

	Lumix::Array<aiMesh*> m_filtered_meshes;
	ImportAssetDialog& m_dialog;
	float m_scale;

}; // struct ConvertTask


ImportAssetDialog::ImportAssetDialog(Lumix::WorldEditor& editor, Metadata& metadata)
	: m_source_exists(false)
	, m_metadata(metadata)
	, m_task(nullptr)
	, m_editor(editor)
	, m_import_physics(false)
	, m_import_model(true)
	, m_is_converting(false)
	, m_is_importing(false)
	, m_is_importing_texture(false)
	, m_mutex(false)
	, m_make_convex(false)
	, m_saved_textures(editor.getAllocator())
	, m_saved_embedded_textures(editor.getAllocator())
	, m_path_mapping(editor.getAllocator())
	, m_mesh_mask(editor.getAllocator())
	, m_convert_to_dds(false)
	, m_convert_to_raw(false)
	, m_raw_texture_scale(1)
	, m_mesh_scale(1)
{
	m_is_opened = false;
	m_message[0] = '\0';
	m_import_message[0] = '\0';
	m_task = nullptr;
	m_source[0] = '\0';
	m_output_dir[0] = '\0';
	m_texture_output_dir[0] = '\0';
}


ImportAssetDialog::~ImportAssetDialog()
{
	if (m_task)
	{
		m_task->destroy();
		LUMIX_DELETE(m_editor.getAllocator(), m_task);
	}
}


bool ImportAssetDialog::checkTexture(const char* source_dir,
	const char* texture_path,
	const char* message)
{
	if (texture_path[0] == '*') return true;

	const char* path = Lumix::PathUtils::isAbsolute(texture_path) || !source_dir
						   ? texture_path
						   : PathBuilder(source_dir) << "/" << texture_path;

	if (PlatformInterface::fileExists(path)) return true;

	char new_path[Lumix::MAX_PATH_LENGTH];
	Lumix::messageBox(message ? message : StringBuilder<Lumix::MAX_PATH_LENGTH + 40>("Texture ")
											  << path
											  << " not found, please locate it");

	if (!PlatformInterface::getOpenFilename(new_path, sizeof(new_path), "All\0*.*\0")) return false;

	Lumix::string old_path_str(path, m_editor.getAllocator());
	Lumix::string new_path_str(new_path, m_editor.getAllocator());
	m_path_mapping.erase(old_path_str);
	m_path_mapping.insert(old_path_str, new_path_str);
	return true;
}


bool ImportAssetDialog::checkTextures()
{
	if (!m_import_materials) return true;

	auto scene = m_importer.GetScene();
	int undefined_count = 0;
	char source_dir[Lumix::MAX_PATH_LENGTH];
	Lumix::PathUtils::getDir(source_dir, sizeof(source_dir), m_source);
	for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
	{
		const aiMaterial* material = scene->mMaterials[i];

		int types[] = {aiTextureType_DIFFUSE, aiTextureType_NORMALS, aiTextureType_HEIGHT};

		for (auto type : types)
		{
			for (unsigned int j = 0; j < material->GetTextureCount((aiTextureType)type); ++j)
			{
				aiString texture_path;
				material->GetTexture((aiTextureType)type, j, &texture_path);
				if (!checkTexture(source_dir, texture_path.C_Str(), nullptr))
				{
					return false;
				}
			}
		}

		if (material->GetTextureCount(aiTextureType_DIFFUSE) != 1)
		{
			PathBuilder texture_filename("undefined");
			texture_filename << undefined_count << ".dds";
			aiString material_name;
			material->Get(AI_MATKEY_NAME, material_name);
			StringBuilder<200> message("Please select diffuse texture for material ");
			message << material_name.C_Str();

			if (!checkTexture(nullptr, texture_filename, message))
			{
				return false;
			}
			++undefined_count;
		}
	}


	return true;
}


static bool isImage(const char* path)
{
	char ext[10];
	Lumix::PathUtils::getExtension(ext, sizeof(ext), path);

	static const char* image_extensions[] = {
		"jpg", "jpeg", "png", "tga", "bmp", "psd", "gif", "hdr", "pic", "pnm"};
	for (auto image_ext : image_extensions)
	{
		if (Lumix::compareString(ext, image_ext) == 0)
		{
			return true;
		}
	}
	return false;
}


void ImportAssetDialog::checkSource()
{
	if (m_output_dir[0] == '\0')
	{
		Lumix::PathUtils::getDir(m_output_dir, sizeof(m_output_dir), m_source);
	}

	m_source_exists = PlatformInterface::fileExists(m_source);

	if (!m_source_exists)
	{
		m_importer.FreeScene();
		return;
	}

	if (isImage(m_source))
	{
		m_importer.FreeScene();
		return;
	}

	ASSERT(!m_task);
	setImportMessage("Importing...");
	m_is_importing = true;
	m_task = LUMIX_NEW(m_editor.getAllocator(), ImportTask)(*this);
	m_task->create("ImportAssetTask");
	m_task->run();
}


void ImportAssetDialog::setMessage(const char* message)
{
	Lumix::MT::SpinLock lock(m_mutex);
	Lumix::copyString(m_message, message);
}


void ImportAssetDialog::setImportMessage(const char* message)
{
	Lumix::MT::SpinLock lock(m_mutex);
	Lumix::copyString(m_import_message, message);
}


void ImportAssetDialog::getMessage(char* msg, int max_size)
{
	Lumix::MT::SpinLock lock(m_mutex);
	Lumix::copyString(msg, max_size, m_message);
}


bool ImportAssetDialog::hasMessage()
{
	Lumix::MT::SpinLock lock(m_mutex);
	return m_message[0] != '\0';
}


void ImportAssetDialog::convert()
{
	ASSERT(!m_task);
	if (!checkTextures()) return;

	setImportMessage("Converting...");
	m_is_converting = true;
	m_task = LUMIX_NEW(m_editor.getAllocator(), ConvertTask)(*this, m_mesh_scale);
	m_task->create("ConvertAssetTask");
	m_task->run();
}


void ImportAssetDialog::importTexture()
{
	ASSERT(!m_task);
	setImportMessage("Importing texture...");

	char dest_path[Lumix::MAX_PATH_LENGTH];
	ImportTextureTask::getDestinationPath(m_output_dir,
		m_source,
		m_convert_to_dds,
		m_convert_to_raw,
		dest_path,
		Lumix::lengthOf(dest_path));

	char tmp[Lumix::MAX_PATH_LENGTH];
	Lumix::PathUtils::normalize(dest_path, tmp, Lumix::lengthOf(tmp));
	m_editor.getRelativePath(dest_path, Lumix::lengthOf(dest_path), tmp);
	Lumix::uint32 hash = Lumix::crc32(dest_path);

	m_metadata.setString(hash, Lumix::crc32("source"), m_source);

	m_is_importing_texture = true;
	m_task = LUMIX_NEW(m_editor.getAllocator(), ImportTextureTask)(*this);
	m_task->create("ImportTextureTask");
	m_task->run();
}


bool ImportAssetDialog::isTextureDirValid() const
{
	if (!m_texture_output_dir[0]) return true;
	char normalized_path[Lumix::MAX_PATH_LENGTH];
	Lumix::PathUtils::normalize(
		m_texture_output_dir, normalized_path, Lumix::lengthOf(normalized_path));

	return (m_editor.isRelativePath(normalized_path));
}


void ImportAssetDialog::onGUI()
{
	if (ImGui::BeginDock("Import Asset", &m_is_opened))
	{
		if (hasMessage())
		{
			char msg[1024];
			getMessage(msg, sizeof(msg));
			ImGui::Text(msg);
			if (ImGui::Button("OK"))
			{
				setMessage("");
			}
			ImGui::EndDock();
			return;
		}

		if (m_is_converting || m_is_importing || m_is_importing_texture)
		{
			if (m_task && m_task->isFinished())
			{
				m_task->destroy();
				LUMIX_DELETE(m_editor.getAllocator(), m_task);
				m_task = nullptr;
				m_is_importing = false;
				m_is_converting = false;
				m_is_importing_texture = false;
			}

			{
				Lumix::MT::SpinLock lock(m_mutex);
				ImGui::Text(m_import_message);
			}
			ImGui::EndDock();
			return;
		}

		if (ImGui::Checkbox("Optimize meshes", &m_optimize_mesh_on_import)) checkSource();
		ImGui::SameLine();
		if (ImGui::Checkbox("Smooth normals", &m_gen_smooth_normal)) checkSource();

		if (ImGui::InputText("Source", m_source, sizeof(m_source))) checkSource();

		ImGui::SameLine();
		if (ImGui::Button("..."))
		{
			PlatformInterface::getOpenFilename(m_source, sizeof(m_source), "All\0*.*\0");
			checkSource();
		}

		if (isImage(m_source))
		{
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
				PlatformInterface::getOpenDirectory(m_output_dir, sizeof(m_output_dir));
			}

			if (ImGui::Button("Import texture"))
			{
				importTexture();
			}
			ImGui::EndDock();
			return;
		}

		if (m_importer.GetScene())
		{
			auto* scene = m_importer.GetScene();
			ImGui::Checkbox("Import model", &m_import_model);
			if (m_import_model)
			{
				ImGui::SameLine();
				ImGui::DragFloat("Scale", &m_mesh_scale, 0.01f, 0.001f, 0);
			}

			if (scene->HasMaterials())
			{
				ImGui::Checkbox(StringBuilder<50>("Import materials (", scene->mNumMaterials, ")"),
					&m_import_materials);
				ImGui::Checkbox("Convert to DDS", &m_convert_to_dds);
			}
			if (scene->HasAnimations())
			{
				ImGui::Checkbox(
					StringBuilder<50>("Import animations (", scene->mNumAnimations, ")"),
					&m_import_animations);
			}
			ImGui::Checkbox("Import physics", &m_import_physics);
			if (m_import_physics)
			{
				ImGui::SameLine();
				ImGui::Checkbox("Make convex", &m_make_convex);
			}

			if (scene->mNumMeshes > 1)
			{
				if (ImGui::CollapsingHeader(
						StringBuilder<30>("Meshes (") << scene->mNumMeshes << ")###Meshes",
						nullptr,
						true,
						true))
				{
					for (int i = 0; i < (int)scene->mNumMeshes; ++i)
					{
						const char* name = scene->mMeshes[i]->mName.C_Str();
						bool b = m_mesh_mask[i];
						ImGui::Checkbox(name[0] == '\0' ? StringBuilder<30>("N/A###na",
															  (Lumix::uint64)&scene->mMeshes[i])
														: name,
							&b);
						m_mesh_mask[i] = b;
					}
				}
			}

			ImGui::InputText("Output directory", m_output_dir, sizeof(m_output_dir));
			ImGui::SameLine();
			if (ImGui::Button("...###browseoutput"))
			{
				PlatformInterface::getOpenDirectory(m_output_dir, sizeof(m_output_dir));
			}

			ImGui::InputText(
				"Texture output directory", m_texture_output_dir, sizeof(m_texture_output_dir));
			ImGui::SameLine();
			if (ImGui::Button("...###browsetextureoutput"))
			{
				PlatformInterface::getOpenDirectory(
					m_texture_output_dir, sizeof(m_texture_output_dir));
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
					convert();
				}
			}
		}
	}
	ImGui::EndDock();
}