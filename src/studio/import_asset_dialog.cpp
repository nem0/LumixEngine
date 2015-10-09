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
#include "ocornut-imgui/imgui.h"
#include "physics/physics_geometry_manager.h"
#include "renderer/model.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "utils.h"


typedef StringBuilder<Lumix::MAX_PATH_LENGTH> PathBuilder;


enum class VertexAttributeDef : uint32_t
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
		StringBuilder<Lumix::MAX_PATH_LENGTH + 50>("Saving ")
		<< data->dest_path
		<< "\n"
		<< int(fraction*100)
		<< "%%");

	return true;
}


static bool saveAsDDS(ImportAssetDialog& dialog,
					  Lumix::FS::FileSystem& fs,
					  const char* source_path,
					  const uint8_t* image_data,
					  int image_width,
					  int image_height,
					  const char* dest_path)
{
	ASSERT(image_data);

	dialog.setImportMessage(
		StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Saving ")
		<< dest_path);

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
	comp_params.m_pImages[0][0] = (uint32_t*)image_data;
	crn_mipmap_params mipmap_params;
	mipmap_params.m_mode = cCRNMipModeGenerateMips;

	void* data = crn_compress(comp_params, mipmap_params, size);
	if (!data)
	{
		dialog.setMessage(
			StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Could not convert ")
			<< source_path);
		return false;
	}

	auto* file = fs.open(fs.getDiskDevice(),
		dest_path,
		Lumix::FS::Mode::WRITE | Lumix::FS::Mode::CREATE);
	if (!file)
	{
		dialog.setMessage(
			StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Could not save ")
			<< dest_path);
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

		char ext[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getExtension(ext, sizeof(ext), source);
		PathBuilder dest_path(output_dir);
		dest_path << "/" << basename << "." << ext;
		Lumix::copyString(out, max_size, dest_path);
	}


	virtual int task() override
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
		virtual bool Update(float percentage) override
		{
			m_task->m_dialog.setImportMessage(
				StringBuilder<50>("Importing... ") << int(percentage*100) << "%%");

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


	~ImportTask()
	{
		m_dialog.m_importer.SetProgressHandler(nullptr);
	}


	virtual int task() override
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
		const aiScene* scene =
			m_dialog.m_importer.ReadFile(m_dialog.m_source, flags);
		if (!scene || !scene->mMeshes || !scene->mMeshes[0]->mTangents)
		{
			m_dialog.setMessage(m_dialog.m_importer.GetErrorString());
			Lumix::g_log_error.log("import")
				<< m_dialog.m_importer.GetErrorString();
		}
		m_dialog.m_mesh_mask.resize(scene->mNumMeshes);
		for (int i = 0; i < m_dialog.m_mesh_mask.size(); ++i)
		{
			m_dialog.m_mesh_mask[i] = true;
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
		uint16_t bone_indices[4];
		int index;
	};


	ConvertTask(ImportAssetDialog& dialog)
		: Task(dialog.m_editor.getAllocator())
		, m_dialog(dialog)
		, m_filtered_meshes(dialog.m_editor.getAllocator())
	{
	}


	bool saveEmbeddedTextures(const aiScene* scene)
	{
		m_dialog.m_saved_embedded_textures.clear();
		for (unsigned int i = 0; i < scene->mNumTextures; ++i)
		{
			const aiTexture* texture = scene->mTextures[i];
			if (texture->mHeight != 0)
			{
				m_dialog.setMessage(
					"Uncompressed texture embedded. This is not supported.");
				return false;
			}
			PathBuilder texture_name("texture"); 
			texture_name << i << ".dds";
			m_dialog.m_saved_embedded_textures.push(
				Lumix::string(texture_name, m_dialog.m_editor.getAllocator()));
			saveAsDDS(m_dialog,
					  m_dialog.m_editor.getEngine().getFileSystem(),
					  "Embedded texture",
					  (const uint8_t*)texture->pcData,
					  texture->mWidth,
					  texture->mHeight,
					  PathBuilder(m_dialog.m_output_dir) << "/" << texture_name);
		}
		return true;
	}


	bool saveTexture(const char* texture_path,
					 const char* source_mesh_dir,
					 Lumix::FS::IFile& material_file) const
	{
		Lumix::string texture_source_path(texture_path,
										  m_dialog.m_editor.getAllocator());
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
			Lumix::fromCString(texture_source_path.c_str() + 1,
							   texture_source_path.length() - 1,
							   &index);
			texture_source_path = m_dialog.m_saved_embedded_textures[index];
		}

		Lumix::PathUtils::FileInfo texture_info(texture_source_path.c_str());
		material_file << "\t, \"texture\" : {\n\t\t\"source\" : \""
					  << texture_info.m_basename << ".";
		material_file << (m_dialog.m_convert_to_dds ? "dds"
													: texture_info.m_extension);
		material_file << "\"\n }\n";

		bool is_already_saved =
			m_dialog.m_saved_textures.indexOf(texture_source_path) >= 0;
		if (is_embedded || is_already_saved) return true;

		PathBuilder source_absolute(source_mesh_dir);
		source_absolute << "/" << texture_source_path.c_str();
		const char* source =
			Lumix::PathUtils::isAbsolute(texture_source_path.c_str())
				? texture_source_path.c_str()
				: source_absolute;

		if (m_dialog.m_convert_to_dds &&
			strcmp(texture_info.m_extension, "dds") != 0)
		{
			PathBuilder dest(m_dialog.m_output_dir);
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
				m_dialog.setMessage(
					StringBuilder<Lumix::MAX_PATH_LENGTH * 2 + 20>(
						"Error converting ", source, " to ", dest));
				return false;
			}
			stbi_image_free(data);
		}
		else
		{
			PathBuilder dest(m_dialog.m_output_dir);
			dest << "/" << texture_info.m_basename << "."
				 << texture_info.m_extension;
			if (strcmp(source, dest) != 0 && !Lumix::copyFile(source, dest))
			{
				m_dialog.setMessage(
					StringBuilder<Lumix::MAX_PATH_LENGTH * 2 + 20>(
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
		
		if (!saveEmbeddedTextures(scene)) return false;
		
		m_dialog.m_saved_textures.clear();
		
		int undefined_count = 0;
		char source_mesh_dir[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getDir(
			source_mesh_dir, sizeof(source_mesh_dir), m_dialog.m_source);

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
			StringBuilder<Lumix::MAX_PATH_LENGTH + 30>("Converting ")
			<< output_material_name);
		auto& fs = m_dialog.m_editor.getEngine().getFileSystem();
		auto* file =
			fs.open(fs.getDiskDevice(),
			output_material_name,
			Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
		if (!file)
		{
			m_dialog.setMessage(
				StringBuilder<20 + Lumix::MAX_PATH_LENGTH>(
				"Could not create ",
				output_material_name));
			return false;
		}

		const aiScene* scene = m_dialog.m_importer.GetScene();
		*file << "{\n\t\"shader\" : \"shaders/"
			<< (isSkinned(scene, material) ? "skinned" : "rigid")
			<< ".shd\"\n";

		if (material->GetTextureCount(aiTextureType_DIFFUSE) == 1)
		{
			aiString texture_path;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &texture_path);
			saveTexture(texture_path.C_Str(), source_mesh_dir, *file);
		}
		else
		{
			saveTexture(PathBuilder("undefined") << *undefined_count << ".dds",
				source_mesh_dir,
				*file);
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
			m_dialog.setMessage(
				StringBuilder<Lumix::MAX_PATH_LENGTH + 20>(
				"Too many normal maps in ", material_name.C_Str()));
			fs.close(*file);
			return false;
		}
		file->write("}", 1);
		fs.close(*file);
		return true;
	}


	virtual int task() override
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


	static bool isSkinned(const aiMesh* mesh)
	{
		return mesh->mNumBones > 0;
	}


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


	void fillSkinInfo(const aiScene* scene,
					  Lumix::Array<SkinInfo>& infos,
					  int vertices_count) const
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


	static uint32_t packUint32(uint8_t _x, uint8_t _y, uint8_t _z, uint8_t _w)
	{
		union
		{
			uint32_t ui32;
			uint8_t arr[4];
		} un;

		un.arr[0] = _x;
		un.arr[1] = _y;
		un.arr[2] = _z;
		un.arr[3] = _w;

		return un.ui32;
	}


	static uint32_t packF4u(const aiVector3D& vec)
	{
		const uint8_t xx = uint8_t(vec.x * 127.0f + 128.0f);
		const uint8_t yy = uint8_t(vec.y * 127.0f + 128.0f);
		const uint8_t zz = uint8_t(vec.z * 127.0f + 128.0f);
		const uint8_t ww = uint8_t(0);
		return packUint32(xx, yy, zz, ww);
	}


	void writeGeometry(Lumix::FS::IFile& file) const
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		int32_t indices_count = 0;
		int vertices_count = 0;
		int32_t vertices_size = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			indices_count += mesh->mNumFaces * 3;
			vertices_count += mesh->mNumVertices;
			vertices_size += mesh->mNumVertices * getVertexSize(mesh);
		}

		file.write((const char*)&indices_count, sizeof(indices_count));
		int32_t polygon_idx = 0;
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

				Lumix::Vec3 position(mesh->mVertices[j].x,
					mesh->mVertices[j].y,
					mesh->mVertices[j].z);
				file.write((const char*)&position, sizeof(position));

				if (mesh->mColors[0])
				{
					auto assimp_color = mesh->mColors[0][j];
					uint8_t color[4];
					color[0] = uint8_t(assimp_color.r * 255);
					color[1] = uint8_t(assimp_color.g * 255);
					color[2] = uint8_t(assimp_color.b * 255);
					color[3] = uint8_t(assimp_color.a * 255);
					file.write(color, sizeof(color));
				}

				auto normal = mesh->mNormals[j];
				uint32_t int_normal = packF4u(normal);
				file.write((const char*)&int_normal, sizeof(int_normal));

				if (mesh->mTangents)
				{
					auto tangent = mesh->mTangents[j];
					uint32_t int_tangent = packF4u(tangent);
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
		static const int NORMAL_SIZE = sizeof(uint8_t) * 4;
		static const int TANGENT_SIZE = sizeof(uint8_t) * 4;
		static const int UV_SIZE = sizeof(float) * 2;
		static const int COLOR_SIZE = sizeof(uint8_t) * 4;
		static const int BONE_INDICES_WEIGHTS_SIZE =
			sizeof(float) * 4 + sizeof(uint16_t) * 4;
		int size = POSITION_SIZE + NORMAL_SIZE + UV_SIZE;
		if (mesh->mTangents) size += TANGENT_SIZE;
		if (mesh->mColors[0]) size += COLOR_SIZE;
		if (isSkinned(mesh)) size += BONE_INDICES_WEIGHTS_SIZE;
		return size;
	}


	void writeMeshes(Lumix::FS::IFile& file) const
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		int32_t mesh_count = 0;
		for (int i = 0; i < m_dialog.m_mesh_mask.size(); ++i)
		{
			if (m_dialog.m_mesh_mask[i]) ++mesh_count;
		}

		file.write((const char*)&mesh_count, sizeof(mesh_count));
		int32_t attribute_array_offset = 0;
		int32_t indices_offset = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			int vertex_size = getVertexSize(mesh);
			aiString material_name;
			scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME,
				material_name);
			int32_t length = (int)strlen(material_name.C_Str());
			file.write((const char*)&length, sizeof(length));
			file.write((const char*)material_name.C_Str(), length);

			file.write((const char*)&attribute_array_offset,
				sizeof(attribute_array_offset));
			int32_t attribute_array_size = mesh->mNumVertices * vertex_size;
			attribute_array_offset += attribute_array_size;
			file.write((const char*)&attribute_array_size,
				sizeof(attribute_array_size));

			file.write((const char*)&indices_offset, sizeof(indices_offset));
			int32_t mesh_tri_count = mesh->mNumFaces;
			indices_offset += mesh->mNumFaces * 3;
			file.write((const char*)&mesh_tri_count, sizeof(mesh_tri_count));

			aiString mesh_name = mesh->mName;
			length = (int)strlen(mesh_name.C_Str());
			file.write((const char*)&length, sizeof(length));
			file.write((const char*)mesh_name.C_Str(), length);

			int32_t attribute_count = getAttributeCount(mesh);
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
		uint32_t length = (int)strlen(attribute_name);
		file.write((const char*)&length, sizeof(length));
		file.write(attribute_name, length);

		uint32_t type = (uint32_t)attribute_type;
		file.write((const char*)&type, sizeof(type));
	}


	static void writeNode(Lumix::FS::IFile& file, const aiNode* node, aiMatrix4x4 parent_transform)
	{
		int32_t len = (int32_t)strlen(node->mName.C_Str());
		file.write((const char*)&len, sizeof(len));
		file.write(node->mName.C_Str(), node->mName.length + 1);

		if (node->mParent)
		{
			int32_t len = (int32_t)strlen(node->mParent->mName.C_Str());
			file.write((const char*)&len, sizeof(len));
			file.write(node->mParent->mName.C_Str(), node->mParent->mName.length);
		}
		else
		{
			int32_t len = 0;
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
			writeNode(
				file, node->mChildren[i], parent_transform * node->mTransformation);
		}
	}


	void writeLods(Lumix::FS::IFile& file) const
	{
		int32_t lods[] = { -1, -1, -1, -1, -1, -1, -1, -1 };
		int32_t lod_count = -1;
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
			int32_t to_mesh = m_filtered_meshes.size() - 1;
			file.write((const char*)&to_mesh, sizeof(to_mesh));
			float distance = FLT_MAX;
			file.write((const char*)&distance, sizeof(distance));
		}
		else
		{
			file.write((const char*)&lod_count, sizeof(lod_count));
			for (int i = 0; i < lod_count; ++i)
			{
				int32_t to_mesh = lods[i];
				file.write((const char*)&to_mesh, sizeof(to_mesh));
				float factor = i == lod_count - 1 ? FLT_MAX : factors[i];
				file.write((const char*)&factor, sizeof(factor));
			}
		}
	}


	void writeSkeleton(Lumix::FS::IFile& file) const
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		int32_t count = countNodes(scene->mRootNode);

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
		header.m_version = (uint32_t)Lumix::PhysicsGeometry::Versions::LAST;
		header.m_convex = (uint32_t)m_dialog.m_make_convex;
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
		PathBuilder phy_path(m_dialog.m_editor.getBasePath());
		phy_path << "/" << filename;
		Lumix::FS::IFile* file =
			fs.open(fs.getDiskDevice(), phy_path, Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
		if (!file)
		{
			Lumix::g_log_error.log("import") << "Could not create file " << phy_path;
			return false;
		}

		const aiScene* scene = m_dialog.m_importer.GetScene();

		writePhysicsHeader(*file);
		int32_t count = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			count += (int32_t)mesh->mNumVertices;
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
		const aiScene* scene = m_dialog.m_importer.GetScene();
		for (auto* mesh : m_filtered_meshes)
		{
			count += (int32_t)mesh->mNumFaces * 3;
		}
		file.write((const char*)&count, sizeof(count));
		int offset = 0;
		for (auto* mesh : m_filtered_meshes)
		{
			for (unsigned int j = 0; j < mesh->mNumFaces; ++j)
			{
				ASSERT(mesh->mFaces[j].mNumIndices == 3);
				uint32_t index = mesh->mFaces[j].mIndices[0] + offset;
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
		const aiScene* scene = m_dialog.m_importer.GetScene();
		for (auto* mesh : m_filtered_meshes)
		{
			if (!mesh->HasNormals())
			{
				m_dialog.setMessage(StringBuilder<256>(
					"Mesh ", mesh->mName.C_Str(), " has no normals."));
				return false;
			}
			if (!mesh->HasPositions())
			{
				m_dialog.setMessage(StringBuilder<256>(
					"Mesh ", mesh->mName.C_Str(), " has no positions."));
				return false;
			}
			if (!mesh->HasTextureCoords(0))
			{
				m_dialog.setMessage(StringBuilder<256>(
					"Mesh ", mesh->mName.C_Str(), " has no texture coords."));
				return false;
			}
		}
		return true;
	}


	static void writeModelHeader(Lumix::FS::IFile& file)
	{
		Lumix::Model::FileHeader header;
		header.m_magic = Lumix::Model::FILE_MAGIC;
		header.m_version = (uint32_t)Lumix::Model::FileVersion::LATEST;
		file.write((const char*)&header, sizeof(header));
	}


	static float getMeshLODFactor(const aiMesh* mesh)
	{
		const char* mesh_name = mesh->mName.C_Str();
		int len = int(strlen(mesh_name));
		if (len < 5) return FLT_MAX;

		const char* last = mesh_name + len - 1;
		while (last > mesh_name && *last >= '0' && *last <= '9')
		{
			--last;
		}
		++last;
		if (last < mesh_name + 4) FLT_MAX;
		if (strncmp(last - 4, "_LOD", 4) != 0) return FLT_MAX;
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


	static int getMeshLOD(const aiMesh* const * mesh_ptr)
	{
		const aiMesh* const mesh = *mesh_ptr;

		const char* mesh_name = mesh->mName.C_Str();
		int len = int(strlen(mesh_name));
		if (len < 5) return -1;

		const char* last = mesh_name + len - 1;
		while (last > mesh_name && *last >= '0' && *last <= '9')
		{
			--last;
		}
		++last;
		if (last < mesh_name + 4) return -1;
		if (strncmp(last - 4, "_LOD", 4) != 0) return -1;

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

		auto cmpMeshes = [](const void* a, const void* b) -> int {
			auto a_mesh = static_cast<aiMesh* const *>(a);
			auto b_mesh = static_cast<aiMesh* const *>(b);
			return getMeshLOD(a_mesh) - getMeshLOD(b_mesh);
		};

		qsort(&m_filtered_meshes[0], m_filtered_meshes.size(), sizeof(m_filtered_meshes[0]), cmpMeshes);
	}


	bool saveLumixModel()
	{
		ASSERT(m_dialog.m_output_dir[0] != '\0');
		if (!checkModel()) return false;

		m_dialog.setImportMessage("Importing model...");
		Lumix::makePath(m_dialog.m_output_dir);

		char basename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getBasename(basename, sizeof(basename), m_dialog.m_source);
		PathBuilder path(m_dialog.m_output_dir);
		path << "/" << basename << ".msh";

		auto& fs = m_dialog.m_editor.getEngine().getFileSystem();
		Lumix::FS::IFile* file =
			fs.open(fs.getDiskDevice(),
					path,
					Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
		if (!file)
		{
			m_dialog.setMessage(
				StringBuilder<Lumix::MAX_PATH_LENGTH + 15>(
					"Failed to open ", path));
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

}; // struct ConvertTask


ImportAssetDialog::ImportAssetDialog(Lumix::WorldEditor& editor, Metadata& metadata)
	: m_source_exists(false)
	, m_metadata(metadata)
	, m_task(nullptr)
	, m_editor(editor)
	, m_import_physics(false)
	, m_is_converting(false)
	, m_is_importing(false)
	, m_is_importing_texture(false)
	, m_mutex(false)
	, m_make_convex(false)
	, m_saved_textures(editor.getAllocator())
	, m_saved_embedded_textures(editor.getAllocator())
	, m_path_mapping(editor.getAllocator())
	, m_mesh_mask(editor.getAllocator())
{
	m_is_opened = false;
	m_message[0] = '\0';
	m_import_message[0] = '\0';
	m_task = nullptr;
	m_source[0] = '\0';
	m_output_dir[0] = '\0';
}


ImportAssetDialog::~ImportAssetDialog()
{
	if (m_task)
	{
		m_task->destroy();
		m_editor.getAllocator().deleteObject(m_task);
	}
}


bool ImportAssetDialog::checkTexture(const char* source_dir, const char* texture_path, const char* message)
{
	const char* path = Lumix::PathUtils::isAbsolute(texture_path) || !source_dir
						   ? texture_path
						   : PathBuilder(source_dir) << "/" << texture_path;

	if (Lumix::fileExists(path)) return true;

	char new_path[Lumix::MAX_PATH_LENGTH];
	Lumix::messageBox(
		message
			? message
			: StringBuilder<Lumix::MAX_PATH_LENGTH + 40>("Texture ")
					<< path
					<< " not found, please locate it");

	if (!Lumix::getOpenFilename(new_path, sizeof(new_path), "All\0*.*\0")) return false;

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

		int types[] = { aiTextureType_DIFFUSE, aiTextureType_NORMALS, aiTextureType_HEIGHT };

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
			StringBuilder<200> message(
				"Please select diffuse texture for material ");
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
		if (strcmp(ext, image_ext) == 0)
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
	
	m_source_exists = Lumix::fileExists(m_source);

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
	m_task = m_editor.getAllocator().newObject<ImportTask>(*this);
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
	m_task = m_editor.getAllocator().newObject<ConvertTask>(*this);
	m_task->create("ConvertAssetTask");
	m_task->run();
}


void ImportAssetDialog::importTexture()
{
	ASSERT(!m_task);
	setImportMessage("Importing texture...");

	char dest_path[Lumix::MAX_PATH_LENGTH];
	ImportTextureTask::getDestinationPath(
		m_output_dir, m_source, m_convert_to_dds, dest_path, Lumix::lengthOf(dest_path));
	
	char tmp[Lumix::MAX_PATH_LENGTH];
	Lumix::PathUtils::normalize(dest_path, tmp, Lumix::lengthOf(tmp));
	m_editor.getRelativePath(dest_path, Lumix::lengthOf(dest_path), tmp);
	uint32_t hash = Lumix::crc32(dest_path);

	m_metadata.setString(hash, Lumix::crc32("source"), m_source);

	m_is_importing_texture = true;
	m_task = m_editor.getAllocator().newObject<ImportTextureTask>(*this);
	m_task->create("ImportTextureTask");
	m_task->run();
}


void ImportAssetDialog::onGUI()
{
	if (!m_is_opened) return;

	if (ImGui::Begin("Import asset", &m_is_opened))
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
			ImGui::End();
			return;
		}

		if (m_is_converting || m_is_importing || m_is_importing_texture)
		{
			if (m_task && m_task->isFinished())
			{
				m_task->destroy();
				m_editor.getAllocator().deleteObject(m_task);
				m_task = nullptr;
				m_is_importing = false;
				m_is_converting = false;
				m_is_importing_texture = false;
			}

			{
				Lumix::MT::SpinLock lock(m_mutex);
				ImGui::Text(m_import_message);
			}
			ImGui::End();
			return;
		}

		if (ImGui::Checkbox("Optimize meshes", &m_optimize_mesh_on_import)) checkSource();
		ImGui::SameLine();
		if (ImGui::Checkbox("Smooth normals", &m_gen_smooth_normal)) checkSource();

		if (ImGui::InputText("Source", m_source, sizeof(m_source))) checkSource();

		ImGui::SameLine();
		if (ImGui::Button("..."))
		{
			Lumix::getOpenFilename(m_source, sizeof(m_source), "All\0*.*\0");
			checkSource();
		}

		if (isImage(m_source))
		{
			ImGui::Checkbox("Convert to DDS", &m_convert_to_dds);
			ImGui::InputText("Output directory", m_output_dir, sizeof(m_output_dir));
			ImGui::SameLine();
			if (ImGui::Button("...##browseoutput"))
			{
				Lumix::getOpenDirectory(m_output_dir, sizeof(m_output_dir));
			}

			if (ImGui::Button("Import texture"))
			{
				importTexture();
			}
			ImGui::End();
			return;
		}

		if (m_importer.GetScene())
		{
			auto* scene = m_importer.GetScene();
			if (scene->HasMaterials())
			{
				ImGui::Checkbox(StringBuilder<50>("Import materials (",
														 scene->mNumMaterials,
														 ")"),
								&m_import_materials);
				ImGui::Checkbox("Convert to DDS", &m_convert_to_dds);
			}
			if (scene->HasAnimations())
			{
				ImGui::Checkbox(StringBuilder<50>("Import animations (",
															 scene->mNumAnimations,
														 ")"),
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
				if (ImGui::CollapsingHeader(StringBuilder<30>("Meshes (")
												<< scene->mNumMeshes
												<< ")##Meshes",
											nullptr,
											true,
											true))
				{
					for (int i = 0; i < (int)scene->mNumMeshes; ++i)
					{
						const char* name = scene->mMeshes[i]->mName.C_Str();
						bool b = m_mesh_mask[i];
						ImGui::Checkbox(name[0] == '\0' ? "N/A" : name, &b);
						m_mesh_mask[i] = b;
					}
				}
			}

			ImGui::InputText("Output directory", m_output_dir, sizeof(m_output_dir));
			ImGui::SameLine();
			if (ImGui::Button("...##browseoutput"))
			{
				Lumix::getOpenDirectory(m_output_dir, sizeof(m_output_dir));
			}
			if (m_output_dir[0] != '\0' && ImGui::Button("Convert"))
			{
				convert();
			}
		}
	}
	ImGui::End();
}