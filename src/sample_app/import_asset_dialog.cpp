#include "import_asset_dialog.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "core/crc32.h"
#include "core/FS/ifile.h"
#include "core/FS/file_system.h"
#include "core/FS/os_file.h"
#include "core/log.h"
#include "core/MT/task.h"
#include "core/path_utils.h"
#include "core/system.h"
#include "debug/floating_points.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "ocornut-imgui/imgui.h"
#include "renderer/model.h"


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


struct ImportTask : public Lumix::MT::Task
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


	ImportTask(ImportAssetDialog& dialog)
		: Task(dialog.m_editor.getAllocator())
		, m_dialog(dialog)
	{
	}


	virtual int task() override
	{
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


	
	void getBoneNamesHashes(const aiNode* node, Lumix::Array<int>& node_names)
	{
		node_names.push(Lumix::crc32(node->mName.C_Str()));
		for (unsigned int i = 0; i < node->mNumChildren; ++i)
		{
			getBoneNamesHashes(node->mChildren[i], node_names);
		}
	}


	void fillSkinInfo(const aiScene* scene,
					  Lumix::Array<SkinInfo>& infos,
					  int vertices_count)
	{

		Lumix::Array<int> node_names(m_dialog.m_editor.getAllocator());
		getBoneNamesHashes(scene->mRootNode, node_names);
		infos.resize(vertices_count);

		int offset = 0;
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
		{
			const aiMesh* mesh = scene->mMeshes[i];
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


	void writeGeometry(Lumix::FS::IFile& file)
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		int32_t indices_count = 0;
		int vertices_count = 0;
		int32_t vertices_size = 0;
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
		{
			indices_count += scene->mMeshes[i]->mNumFaces * 3;
			vertices_count += scene->mMeshes[i]->mNumVertices;
			vertices_size +=
				scene->mMeshes[i]->mNumVertices * getVertexSize(scene->mMeshes[i]);
		}

		file.write((const char*)&indices_count, sizeof(indices_count));
		int32_t polygon_idx = 0;
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
		{
			const aiMesh* mesh = scene->mMeshes[i];
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
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
		{
			const aiMesh* mesh = scene->mMeshes[i];
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
		if (isSkinned(mesh))
		{
			count += 2;
		}
		if (mesh->mTangents)
		{
			count += 1;
		}
		return count;
	}


	static int getVertexSize(const aiMesh* mesh)
	{
		static const int POSITION_SIZE = sizeof(float) * 3;
		static const int NORMAL_SIZE = sizeof(uint8_t) * 4;
		static const int TANGENT_SIZE = sizeof(uint8_t) * 4;
		static const int UV_SIZE = sizeof(float) * 2;
		static const int BONE_INDICES_WEIGHTS_SIZE =
			sizeof(float) * 4 + sizeof(uint16_t) * 4;
		int size = POSITION_SIZE + NORMAL_SIZE + UV_SIZE;
		if (mesh->mTangents)
		{
			size += TANGENT_SIZE;
		}
		if (isSkinned(mesh))
		{
			size += BONE_INDICES_WEIGHTS_SIZE;
		}
		return size;
	}


	void writeMeshes(Lumix::FS::IFile& file)
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		int32_t mesh_count = (int32_t)scene->mNumMeshes;

		file.write((const char*)&mesh_count, sizeof(mesh_count));
		int32_t attribute_array_offset = 0;
		int32_t indices_offset = 0;
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
		{
			const aiMesh* mesh = scene->mMeshes[i];
			int vertex_size = getVertexSize(mesh);
			aiString material_name;
			scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME,
				material_name);
			int32_t length = strlen(material_name.C_Str());
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
			length = strlen(mesh_name.C_Str());
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
			writeAttribute("in_normal", VertexAttributeDef::BYTE4, file);
			if (mesh->mTangents)
			{
				writeAttribute("in_tangents", VertexAttributeDef::BYTE4, file);
			}
			writeAttribute("in_tex_coords", VertexAttributeDef::FLOAT2, file);
		}
	}



	static void writeAttribute(const char* attribute_name,
		VertexAttributeDef attribute_type,
		Lumix::FS::IFile& file)
	{
		uint32_t length = strlen(attribute_name);
		file.write((const char*)&length, sizeof(length));
		file.write(attribute_name, length);

		uint32_t type = (uint32_t)attribute_type;
		file.write((const char*)&type, sizeof(type));
	}


	static void
		writeNode(Lumix::FS::IFile& file, const aiNode* node, aiMatrix4x4 parent_transform)
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


	void writeSkeleton(Lumix::FS::IFile& file)
	{
		const aiScene* scene = m_dialog.m_importer.GetScene();
		int32_t count = countNodes(scene->mRootNode);

		if (count == 1)
		{
			TODO("todo");
			count = 0;
		}
		file.write((const char*)&count, sizeof(count));
		if (count > 0)
		{
			writeNode(file, scene->mRootNode, aiMatrix4x4());
		}
	}

	bool saveLumixModel()
	{
		/*QDir().mkpath(m_destination);
		if (!m_import_model)
		{
			emit progress(2 / 3.0f, "");
			return true;
		}

		if (!checkModel())
		{
			return false;
		}
		*/

		
		char path[Lumix::MAX_PATH_LENGTH];
		char basename[Lumix::MAX_PATH_LENGTH];
		Lumix::PathUtils::getBasename(basename, sizeof(basename), m_dialog.m_source);
		Lumix::copyString(path, sizeof(path), m_dialog.m_editor.getBasePath());
		Lumix::catString(path, sizeof(path), "/");
		if (m_dialog.m_output_dir[0] != '\0')
		{
			Lumix::catString(path, sizeof(path), m_dialog.m_output_dir);
			Lumix::catString(path, sizeof(path), "/");
		}
		Lumix::catString(path, sizeof(path), basename);
		Lumix::catString(path, sizeof(path), ".msh");

		auto& engine = m_dialog.m_editor.getEngine();
		Lumix::FS::IFile* file = engine.getFileSystem().open(
			engine.getFileSystem().getDiskDevice(),
			path,
			Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);

		if (!file)
		{
			//m_error_message = QString("Failed to open %1").arg(dest);
			return false;
		}
		//auto dest = m_destination + "/" + source_path.baseName() + ".msh";
		Lumix::Model::FileHeader header;
		header.m_magic = Lumix::Model::FILE_MAGIC;
		header.m_version = (uint32_t)Lumix::Model::FileVersion::LATEST;

		file->write((const char*)&header, sizeof(header));

		writeMeshes(*file);
		writeGeometry(*file);

		writeSkeleton(*file);

		int32_t lod_count = 1;
		file->write((const char*)&lod_count, sizeof(lod_count));
		int32_t to_mesh = m_dialog.m_importer.GetScene()->mNumMeshes - 1;
		file->write((const char*)&to_mesh, sizeof(to_mesh));
		float distance = FLT_MAX;
		file->write((const char*)&distance, sizeof(distance));

		engine.getFileSystem().close(*file);
		return true;
	}

	ImportAssetDialog& m_dialog;
};


ImportAssetDialog::ImportAssetDialog(Lumix::WorldEditor& editor)
	: m_source_exists(false)
	, m_task(nullptr)
	, m_editor(editor)
{
	m_source[0] = '\0';
	m_output_dir[0] = '\0';
}


void ImportAssetDialog::checkSource()
{
	m_source_exists = Lumix::fileExists(m_source);
	if (!m_source_exists) return;

	Lumix::enableFloatingPointTraps(false);
	m_importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS,
		aiComponent_COLORS | aiComponent_LIGHTS |
		aiComponent_CAMERAS);
	const aiScene* scene = m_importer.ReadFile(
		m_source,
		aiProcess_JoinIdenticalVertices | aiProcess_RemoveComponent |
		aiProcess_GenUVCoords | aiProcess_RemoveRedundantMaterials |
		aiProcess_Triangulate | aiProcess_LimitBoneWeights |
		aiProcess_OptimizeGraph | aiProcess_OptimizeMeshes |
		aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
	if (!scene || !scene->mMeshes || !scene->mMeshes[0]->mTangents)
	{
		//m_error_message = m_importer.GetErrorString();
		Lumix::g_log_error.log("import") << m_importer.GetErrorString();
	}
	Lumix::enableFloatingPointTraps(true);
}


void ImportAssetDialog::import()
{
	ImportTask t(*this);
	t.saveLumixModel();
}


void ImportAssetDialog::onGui()
{
	if (ImGui::Begin("Import asset"))
	{
		if (ImGui::InputText("Source", m_source, sizeof(m_source))) checkSource();

		ImGui::SameLine();
		if (ImGui::Button("..."))
		{
			Lumix::getOpenFilename(m_source, sizeof(m_source), "All\0*.*\0");
			checkSource();
		}

		if (m_importer.GetScene())
		{
			auto* scene = m_importer.GetScene();
			bool b;
			ImGui::Checkbox("Create directory", &b);
			if (scene->HasMaterials())
			{
				ImGui::Checkbox("Import materials", &b);
				ImGui::Checkbox("Convert to DDS", &b);
			}
			if (scene->HasAnimations())
			{
				ImGui::Checkbox("Import animation", &b);
			}
			ImGui::Checkbox("Import physics", &b);

			if (ImGui::CollapsingHeader("Meshes", nullptr, true, true))
			{
				for (int i = 0; i < (int)scene->mNumMeshes; ++i)
				{
					const char* name = scene->mMeshes[i]->mName.C_Str();
					ImGui::Checkbox(name[0] == '\0' ? "N/A" : name, &b);
				}
			}

			ImGui::InputText("Output directory", m_output_dir, sizeof(m_output_dir));
			if (ImGui::Button("Import"))
			{
				import();
			}
		}
	}
	ImGui::End();
}