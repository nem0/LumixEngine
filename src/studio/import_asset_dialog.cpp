#include "import_asset_dialog.h"
#include "ui_import_asset_dialog.h"
#include "animation/animation.h"
#include "assimp/defaultlogger.hpp"
#include "assimp/postprocess.h"
#include "assimp/progresshandler.hpp"
#include "assimp/scene.h"
#include "core/log.h"
#include "core/vec3.h"
#include "crnlib.h"
#include "debug/floating_points.h"
#include "graphics/model.h"
#include <qfile.h>
#include <qfiledialog.h>
#include <qimagereader.h>
#include <qimagewriter.h>
#include <qmessagebox.h>
#include <qprocess.h>
#include <qthread.h>


static const int RIGID_VERTEX_SIZE = 24;
static const int SKINNED_VERTEX_SIZE = 56;


class LogStream : public Assimp::LogStream
{
	public:
		LogStream(ImportThread& thread)
			: m_thread(thread)
		{}

		void write(const char* message) override
		{
			Lumix::g_log_info.log("import") << message;
		}

		ImportThread& m_thread;
};


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


struct SkinInfo
{
	SkinInfo()
	{
		memset(this, 0, sizeof(SkinInfo));
		index = 0;
	}

	float weights[4];
	int bone_indices[4];
	int index;
};


static void writeAttribute(const char* attribute_name, VertexAttributeDef attribute_type, QFile& file)
{
	uint32_t length = strlen(attribute_name);
	file.write((const char*)&length, sizeof(length));
	file.write(attribute_name, length);

	uint32_t type = (uint32_t)attribute_type;
	file.write((const char*)&type, sizeof(type));
}


ImportThread::ImportThread(ImportAssetDialog& dialog)
	: m_dialog(dialog)
	, m_importer(dialog.getImporter())
{
	Assimp::Logger::LogSeverity severity = Assimp::Logger::NORMAL;
	Assimp::DefaultLogger::create("", severity, aiDefaultLogStream_DEBUGGER);
	m_log_stream = new LogStream(*this);
	m_importer.SetProgressHandler(this);
}


ImportThread::~ImportThread()
{
	m_importer.SetProgressHandler(nullptr);
	delete m_log_stream;
}


static int getVertexSize(const aiScene* scene)
{
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
	{
		if (scene->mMeshes[i]->mNumBones > 0)
		{
			return SKINNED_VERTEX_SIZE;
		}
	}
	return RIGID_VERTEX_SIZE;
}


void ImportThread::writeMeshes(QFile& file)
{
	const aiScene* scene = m_importer.GetScene();
	int32_t mesh_count = (int32_t)scene->mNumMeshes;
	int vertex_size = getVertexSize(scene);

	file.write((const char*)&mesh_count, sizeof(mesh_count));
	int32_t attribute_array_offset = 0;
	int32_t indices_offset = 0;
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
	{
		const aiMesh* mesh = scene->mMeshes[i];
		aiString material_name;
		scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, material_name);
		int32_t length = strlen(material_name.C_Str());
		file.write((const char*)&length, sizeof(length));
		file.write((const char*)material_name.C_Str(), length);

		file.write((const char*)&attribute_array_offset, sizeof(attribute_array_offset));
		int32_t attribute_array_size = mesh->mNumVertices * vertex_size;
		attribute_array_offset += attribute_array_size;
		file.write((const char*)&attribute_array_size, sizeof(attribute_array_size));

		file.write((const char*)&indices_offset, sizeof(indices_offset));
		int32_t mesh_tri_count = mesh->mNumFaces;
		indices_offset += mesh->mNumFaces * 3;
		file.write((const char*)&mesh_tri_count, sizeof(mesh_tri_count));

		aiString mesh_name = material_name;
		length = strlen(mesh_name.C_Str());
		file.write((const char*)&length, sizeof(length));
		file.write((const char*)mesh_name.C_Str(), length);

		int32_t attribute_count = vertex_size == SKINNED_VERTEX_SIZE ? 6 : 4;
		file.write((const char*)&attribute_count, sizeof(attribute_count));

		if (vertex_size == SKINNED_VERTEX_SIZE)
		{
			writeAttribute("in_weights", VertexAttributeDef::FLOAT4, file);
			writeAttribute("in_indices", VertexAttributeDef::INT4, file);
		}

		writeAttribute("in_position", VertexAttributeDef::POSITION, file);
		writeAttribute("in_normal", VertexAttributeDef::BYTE4, file);
		writeAttribute("in_tangents", VertexAttributeDef::BYTE4, file);
		writeAttribute("in_tex_coords", VertexAttributeDef::SHORT2, file);
	}
}


QVector<QString> getBoneNames(const aiScene* scene)
{
	struct S {
		static void x(const aiNode* node, QVector<QString>& node_names) 
		{
			node_names.push_back(node->mName.C_Str());
			for (unsigned int i = 0; i < node->mNumChildren; ++i)
			{
				x(node->mChildren[i], node_names);
			}
		}
	};
	
	QVector<QString> names;
	S::x(scene->mRootNode, names);
	return names;
}


static void fillSkinInfo(const aiScene* scene, QVector<SkinInfo>& infos, int vertices_count)
{
	QVector<QString> node_names = getBoneNames(scene);
	infos.resize(vertices_count);

	int offset = 0;
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
	{
		const aiMesh* mesh = scene->mMeshes[i];
		for (unsigned int j = 0; j < mesh->mNumBones; ++j)
		{
			const aiBone* bone = mesh->mBones[j];
			int bone_index = node_names.indexOf(bone->mName.C_Str());
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


void ImportThread::writeGeometry(QFile& file)
{
	const aiScene* scene = m_importer.GetScene();
	int vertex_size = getVertexSize(scene);
	int32_t indices_count = 0;
	int32_t vertices_size = 0;
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
	{
		indices_count += scene->mMeshes[i]->mNumFaces * 3;
		vertices_size += scene->mMeshes[i]->mNumVertices * vertex_size;
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

	QVector<SkinInfo> skin_infos;
	fillSkinInfo(scene, skin_infos, vertices_size / vertex_size);

	int skin_index = 0;
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
	{
		const aiMesh* mesh = scene->mMeshes[i];
		for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
		{
			if (vertex_size == SKINNED_VERTEX_SIZE)
			{
				file.write((const char*)skin_infos[skin_index].weights, sizeof(skin_infos[skin_index].weights));
				file.write((const char*)skin_infos[skin_index].bone_indices, sizeof(skin_infos[skin_index].bone_indices));
				++skin_index;
			}

			Lumix::Vec3 position(mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z);
			file.write((const char*)&position, sizeof(position));

			auto normal = mesh->mNormals[j];
			uint8_t byte_normal[4] = { (int8_t)(normal.x * 127), (int8_t)(normal.y * 127), (int8_t)(normal.z * 127), 0 };
			file.write((const char*)byte_normal, sizeof(byte_normal));

			auto tangent = mesh->mTangents[j];
			uint8_t byte_tangent[4] = { (int8_t)(tangent.x * 127), (int8_t)(tangent.y * 127), (int8_t)(tangent.z * 127), 0 };
			file.write((const char*)byte_tangent, sizeof(byte_tangent));

			auto uv = mesh->mTextureCoords[0][j];
			short short_uv[2] = { (short)(uv.x * 2048), (short)(uv.y * 2048) };
			file.write((const char*)short_uv, sizeof(short_uv));
		}
	}
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


static void writeNode(QFile& file, const aiNode* node, aiMatrix4x4 parent_transform)
{
	int32_t len = (int32_t)strlen(node->mName.C_Str());
	file.write((const char*)&len, sizeof(len));
	file.write(node->mName.C_Str());

	if (node->mParent)
	{
		int32_t len = (int32_t)strlen(node->mParent->mName.C_Str());
		file.write((const char*)&len, sizeof(len));
		file.write(node->mParent->mName.C_Str());
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
		writeNode(file, node->mChildren[i], parent_transform * node->mTransformation);
	}
}



void ImportThread::writeSkeleton(QFile& file)
{
	const aiScene* scene = m_importer.GetScene();
	int32_t count = countNodes(scene->mRootNode);
	file.write((const char*)&count, sizeof(count));
	writeNode(file, scene->mRootNode, aiMatrix4x4());
}


bool ImportThread::saveLumixMesh()
{
	QFileInfo source_path(m_source);
	auto dest = m_destination + "/" + source_path.baseName() + ".msh";
	QDir().mkpath(m_destination);
	QFile file(dest);
	if (!file.open(QIODevice::WriteOnly))
	{
		m_error_message = QString("Failed to open %1").arg(dest);
		return false;
	}
	Lumix::Model::FileHeader header;
	header.m_magic = Lumix::Model::FILE_MAGIC;
	header.m_version = (uint32_t)Lumix::Model::FileVersion::LATEST;

	file.write((const char*)&header, sizeof(header));

	emit progress(1 / 3.0f + 1 / 9.0f, "Saving mesh...");
	writeMeshes(file);
	emit progress(1 / 3.0f + 2 / 9.0f, "Saving mesh...");
	writeGeometry(file);

	writeSkeleton(file);

	int32_t lod_count = 1;
	file.write((const char*)&lod_count, sizeof(lod_count));
	int32_t to_mesh = m_importer.GetScene()->mNumMeshes - 1;
	file.write((const char*)&to_mesh, sizeof(to_mesh));
	float distance = FLT_MAX;
	file.write((const char*)&distance, sizeof(distance));

	file.close();
	emit progress(2 / 3.0f, "Mesh saved.");
	return true;
}


static bool convertToDDS(const QString& src, const QString& dest, crn_progress_callback_func callback, void* callback_data)
{
	QImage image(src);
	if (image.isNull())
	{
		return false;
	}

	crn_uint32 size;
	crn_comp_params comp_params;
	comp_params.m_width = image.width();
	comp_params.m_height = image.height();
	comp_params.m_file_type = cCRNFileTypeDDS;
	comp_params.m_format = cCRNFmtDXT3;
	comp_params.m_quality_level = cCRNMinQualityLevel;
	comp_params.m_dxt_quality = cCRNDXTQualityNormal;
	comp_params.m_dxt_compressor_type = cCRNDXTCompressorRYG;
	comp_params.m_pProgress_func = callback;
	comp_params.m_pProgress_func_data = callback_data;
	comp_params.m_num_helper_threads = 4;
	QVector<uint32_t> img_data;
	img_data.resize(image.width() * image.height());
	for (int j = 0; j < image.height(); ++j)
	{
		for (int i = 0; i < image.width(); ++i)
		{
			QRgb rgb = image.pixel(i, j);
			img_data[i + j * image.width()] = qRed(rgb) | (qGreen(rgb) << 8) | (qBlue(rgb) << 16) | (qAlpha(rgb) << 24);
		}
	}
	comp_params.m_pImages[0][0] = &img_data[0];
	crn_mipmap_params mipmap_params;
	mipmap_params.m_mode = cCRNMipModeGenerateMips;

	void* data = crn_compress(comp_params, mipmap_params, size);

	QFile file(dest);
	if (data && file.open(QIODevice::WriteOnly))
	{
		file.write((const char*)data, size);
		file.close();
		crn_free_block(data);
		return true;
	}
	else
	{
		crn_free_block(data);
		return false;
	}
}


bool ImportThread::saveTexture(const QString& texture_path, const QFileInfo& material_info,  QFile& material_file, bool is_normal_map)
{
	QFileInfo texture_info(texture_path);

	QDir().mkpath(m_destination + "/" + texture_info.path());
	QString texture_entry = QString("\t, \"texture\" : {\n\t\t\"source\" : \"%1\"\n")
		.arg(m_convert_texture_to_DDS ? texture_info.path() + "/" + texture_info.baseName() + ".dds" : texture_path);
	if (is_normal_map)
	{
		texture_entry += "\t\t, \"uniform\" : \"normalmap\"\n";
	}
	texture_entry += "\t}\n";
	if (is_normal_map)
	{
		material_file.write("\t, \"normal_mapping\" : true\n");
	}
	material_file.write(texture_entry.toLatin1().data());
	if (m_convert_texture_to_DDS && texture_info.suffix() != "dds")
	{
		auto source = material_info.path() + "/" + texture_path;
		auto dest = m_destination + "/" + texture_info.path() + "/" + texture_info.baseName() + ".dds";
		if (!convertToDDS(source, dest, nullptr, nullptr))
		{
			m_error_message = QString("Error converting %1 to %2").arg(source).arg(dest);
			return false;
		}
	}
	else
	{
		auto source = material_info.dir().path() + "/" + texture_path;
		auto dest = m_destination + "/" + texture_path;
		if (source != dest)
		{
			QFile afile(dest);
			if (afile.exists())
			{
				auto p = afile.permissions();
				p |= QFile::WriteOwner;
				afile.setPermissions(p);
				afile.remove();
			}
			if (!QFile::copy(source, dest))
			{
				m_error_message = QString("Error copying %1 to %2").arg(source).arg(dest);
				return false;
			}
		}
	}
	return true;
}


bool ImportThread::saveLumixMaterials()
{
	static const float PROGRESS_FROM = 0.5f;
	static const float PROGRESS_LENGTH = 0.5f;

	if (!m_import_materials)
	{
		return true;
	}

	QFileInfo source_info(m_source);
	const aiScene* scene = m_importer.GetScene();
	int vertex_size = getVertexSize(scene);

	for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
	{
		const aiMaterial* material = scene->mMaterials[i];
		aiString material_name;
		material->Get(AI_MATKEY_NAME, material_name);
		emit progress(PROGRESS_FROM + PROGRESS_LENGTH * i / (float)scene->mNumMaterials, QString("Saving material %1...").arg(material_name.C_Str()));
		auto output_material_name = m_destination + "/" + material_name.C_Str() + ".mat";
		QFile file(output_material_name);
		if (file.open(QIODevice::WriteOnly))
		{
			file.write(QString("{\n\t\"shader\" : \"shaders/%1.shd\"\n").arg(vertex_size == SKINNED_VERTEX_SIZE ? "skinned" : "rigid").toLatin1().data());
			if(material->GetTextureCount(aiTextureType_DIFFUSE) == 1)
			{
				aiString texture_path;
				material->GetTexture(aiTextureType_DIFFUSE, 0, &texture_path);
				saveTexture(texture_path.C_Str(), source_info, file, false);
			}
			else
			{
				saveTexture("undefined.dds", source_info, file, false);
			}

			if (material->GetTextureCount(aiTextureType_NORMALS) == 1)
			{
				aiString texture_path;
				material->GetTexture(aiTextureType_NORMALS, 0, &texture_path);
				saveTexture(texture_path.C_Str(), source_info, file, true);
			}
			else if (material->GetTextureCount(aiTextureType_HEIGHT) == 1)
			{
				aiString texture_path;
				material->GetTexture(aiTextureType_HEIGHT, 0, &texture_path);
				saveTexture(texture_path.C_Str(), source_info, file, true);
			}
			else if (material->GetTextureCount(aiTextureType_NORMALS) > 1)
			{
				m_error_message = QString("Too many normal maps in %1").arg(material_name.C_Str());
				file.close();
				return false;
			}
			file.write("}");
			file.close();
		}
		else
		{
			m_error_message = QString("Error writing %1").arg(output_material_name);
			return false;
		}
	}
	return true;
}


void ImportThread::run()
{
	m_error_message = "";
	Assimp::DefaultLogger::get()->attachStream(m_log_stream);
	if (!m_importer.GetScene())
	{
		Lumix::enableFloatingPointTraps(false);
		const aiScene* scene = m_importer.ReadFile(m_source.toLatin1().data(), aiProcess_RemoveRedundantMaterials | aiProcess_Triangulate | aiProcess_LimitBoneWeights | aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
		if (!scene || !scene->mMeshes || !scene->mMeshes[0]->mTangents)
		{
			m_error_message = m_importer.GetErrorString();
			Lumix::g_log_error.log("import") << m_importer.GetErrorString();
		}
		Lumix::enableFloatingPointTraps(true);
	}
	else
	{
		if (saveLumixMesh())
		{
			saveLumixMaterials();
		}
	}
	Assimp::DefaultLogger::get()->detatchStream(m_log_stream);
}
		

ImportAssetDialog::ImportAssetDialog(QWidget* parent, const QString& base_path)
	: QDialog(parent)
	, m_base_path(base_path)
{
	m_import_thread = new ImportThread(*this);
	m_ui = new Ui::ImportAssetDialog;
	m_ui->setupUi(this);

	m_ui->importMaterialsCheckbox->hide();
	m_ui->importAnimationCheckbox->hide();
	m_ui->convertToDDSCheckbox->hide();
	m_ui->importButton->setEnabled(false);

	connect(m_import_thread, &ImportThread::progress, this, &ImportAssetDialog::on_progressUpdate, Qt::QueuedConnection);
	connect(m_import_thread, &QThread::finished, this, &ImportAssetDialog::on_importFinished, Qt::QueuedConnection);
	m_ui->destinationInput->setText(QDir::currentPath());
	on_progressUpdate(1, "");
}


static bool isTexture(const QFileInfo& info)
{
	auto supported_formats = QImageReader::supportedImageFormats();
	auto suffix = info.suffix();
	for (auto format : supported_formats)
	{
		if (format == suffix)
		{
			return true;
		}
	}
	return false;
}


void ImportAssetDialog::on_sourceInput_textChanged(const QString& text)
{
	m_ui->importButton->setEnabled(false); 
	m_ui->importMaterialsCheckbox->hide();
	m_ui->convertToDDSCheckbox->hide();
	m_ui->importAnimationCheckbox->hide();
	if (QFile::exists(text))
	{
		QFileInfo info(text);

		if (isTexture(info))
		{
			m_ui->importButton->setEnabled(true);
		}
		else
		{
			m_importer.FreeScene();
			m_import_thread->setSource(text);
			m_import_thread->start();
		}
	}
}


void ImportAssetDialog::on_importFinished()
{
	if (m_import_thread->getErrorMessage().isEmpty())
	{
		m_ui->importButton->setEnabled(true);
		m_ui->importAnimationCheckbox->show();
		m_ui->importAnimationCheckbox->setEnabled(m_importer.GetScene()->HasAnimations());
		m_ui->importMaterialsCheckbox->setText(QString("Import %1 materials").arg(m_importer.GetScene()->mNumMaterials));
		m_ui->importMaterialsCheckbox->show();
		m_ui->convertToDDSCheckbox->show();
		m_ui->convertToDDSCheckbox->setEnabled(m_ui->importMaterialsCheckbox->isChecked());
		m_ui->statusLabel->setText("Done.");
	}
	else
	{
		m_ui->statusLabel->setText(m_import_thread->getErrorMessage());
	}
	m_ui->progressBar->setValue(100);
}


void ImportAssetDialog::on_progressUpdate(float percentage, QString message)
{
	m_ui->statusLabel->setText(message);
	m_ui->progressBar->setValue(percentage > 0 ? 100 * percentage : 5);
}


void ImportAssetDialog::on_browseSourceButton_clicked()
{
	QString path = QFileDialog::getOpenFileName(this, "Select source", QString(), "All files (*.*)");
	if (!path.isEmpty())
	{
		m_ui->sourceInput->setText(path);
	}
}


void ImportAssetDialog::on_importMaterialsCheckbox_stateChanged(int)
{
	if (m_ui->importMaterialsCheckbox->isChecked())
	{
		m_ui->convertToDDSCheckbox->setEnabled(true);
	}
	else
	{
		m_ui->convertToDDSCheckbox->setEnabled(false);
	}
}


void ImportAssetDialog::on_browseDestinationButton_clicked()
{
	QString path = QFileDialog::getExistingDirectory(this, "Select destination", QDir::currentPath());
	if (!path.isEmpty())
	{
		m_ui->destinationInput->setText(path);
	}
}


void ImportAssetDialog::setDestination(const QString& destination)
{
	m_ui->destinationInput->setText(destination);
}


void ImportAssetDialog::setSource(const QString& source)
{
	m_ui->sourceInput->setText(source);
}


void ImportAssetDialog::importModel()
{
	m_import_thread->setDestination(m_ui->destinationInput->text());
	m_import_thread->setSource(m_ui->sourceInput->text());
	m_import_thread->setConvertTexturesToDDS(m_ui->convertToDDSCheckbox->isChecked());
	m_import_thread->setImportMaterials(m_ui->importMaterialsCheckbox->isChecked());
	m_import_thread->start();
}


Lumix::Vec3 getPosition(const aiNodeAnim* channel, float frame)
{
	unsigned int i = 0;
	while (frame > (float)channel->mPositionKeys[i].mTime && i < channel->mNumPositionKeys)
	{
		++i;
	}
	auto first = channel->mPositionKeys[i].mValue;

	if (i + 1 == channel->mNumPositionKeys)
	{
		return Lumix::Vec3(first.x, first.y, first.z);
	}
	auto second = channel->mPositionKeys[i + 1].mValue;
	float t = (frame - channel->mPositionKeys[i].mTime) / (channel->mPositionKeys[i + 1].mTime - channel->mPositionKeys[i].mTime);
	first *= 1 - t;
	second *= t;

	first += second;

	return Lumix::Vec3(first.x, first.y, first.z);
}


Lumix::Quat getRotation(const aiNodeAnim* channel, float frame)
{
	unsigned int i = 0;
	while (frame > (float)channel->mRotationKeys[i].mTime && i < channel->mNumRotationKeys)
	{
		++i;
	}
	auto first = channel->mRotationKeys[i].mValue;

	if (i + 1 == channel->mNumRotationKeys)
	{
		return Lumix::Quat(first.x, first.y, first.z, first.w);
	}

	auto second = channel->mRotationKeys[i + 1].mValue;
	float t = (frame - channel->mRotationKeys[i].mTime) / (channel->mRotationKeys[i + 1].mTime - channel->mRotationKeys[i].mTime);
	aiQuaternion out;
	aiQuaternion::Interpolate(out, first, second, t);

	return Lumix::Quat(out.x, out.y, out.z, out.w);
}


void ImportAssetDialog::importAnimation()
{
	on_progressUpdate(0.9f, "Importing animations...");
	Q_ASSERT(!m_ui->sourceInput->text().isEmpty());

	QVector<QString> bone_names = getBoneNames(m_importer.GetScene());

	for (unsigned int i = 0; i < m_importer.GetScene()->mNumAnimations; ++i)
	{
		const aiAnimation* animation = m_importer.GetScene()->mAnimations[0];
		on_progressUpdate(0.9f + 0.1f * (i / m_importer.GetScene()->mNumAnimations), QString("Importing animation %1...").arg(animation->mName.C_Str()));
		QFile file(m_ui->destinationInput->text() + "/" + animation->mName.C_Str() + ".ani");
		if (file.open(QIODevice::WriteOnly))
		{
			Lumix::Animation::Header header;
			float fps = animation->mTicksPerSecond == 0 ? 25 : animation->mTicksPerSecond;
			header.fps = fps;
			header.magic = Lumix::Animation::HEADER_MAGIC;
			header.version = 1;
			file.write((const char*)&header, sizeof(header));
			int32_t frame_count = animation->mDuration;
			file.write((const char*)&frame_count, sizeof(frame_count));
			int32_t bone_count = animation->mNumChannels;
			file.write((const char*)&bone_count, sizeof(bone_count));

			QVector<Lumix::Vec3> positions;
			QVector<Lumix::Quat> rotations;

			positions.resize(bone_count * frame_count);
			rotations.resize(bone_count * frame_count);

			for (unsigned int channel_idx = 0; channel_idx < animation->mNumChannels; ++channel_idx)
			{
				const aiNodeAnim* channel = animation->mChannels[channel_idx];
				int bone_index = bone_names.indexOf(channel->mNodeName.C_Str());
				for (int frame = 0; frame < frame_count; ++frame)
				{
					positions[frame * bone_count + bone_index] = getPosition(channel, frame);
					rotations[frame * bone_count + bone_index] = getRotation(channel, frame);
				}
			}
			
			file.write((const char*)&positions[0], sizeof(positions[0]) * positions.size());
			file.write((const char*)&rotations[0], sizeof(rotations[0]) * rotations.size());
			for (int i = 0; i < bone_names.size(); ++i)
			{
				uint32_t hash = crc32(bone_names[i].toLatin1().data());
				file.write((const char*)&hash, sizeof(hash));
			}

			file.close();
		}
	}
	on_progressUpdate(1.0f, "Animation imported.");
}


void ImportAssetDialog::importTexture()
{
	Lumix::enableFloatingPointTraps(false);
	Q_ASSERT(!m_ui->sourceInput->text().isEmpty());

	on_progressUpdate(0.01f, "Importing texture...");
	QCoreApplication::processEvents();

	QFileInfo source_info(m_ui->sourceInput->text());
	struct Callback
	{
		static crn_bool foo(crn_uint32 phase_index, crn_uint32 total_phases, crn_uint32 subphase_index, crn_uint32 total_subphases, void* pUser_data_ptr)
		{
			static_cast<ImportAssetDialog*>(pUser_data_ptr)->on_progressUpdate(phase_index / (float)total_phases + subphase_index / (float)total_subphases / total_phases, "Importing texture...");
			QCoreApplication::processEvents();
			return true;
		}
	};
	if (convertToDDS(m_ui->sourceInput->text(), m_ui->destinationInput->text() + "/" + source_info.baseName() + ".dds", &Callback::foo, this))
	{
		on_progressUpdate(1.0f, "Import successful.");
	}
	else
	{
		on_progressUpdate(1.0f, "Import failed.");
	}
	Lumix::enableFloatingPointTraps(true);
}


void ImportAssetDialog::on_importButton_clicked()
{
	Q_ASSERT(!m_ui->destinationInput->text().isEmpty());

	QFileInfo source_info(m_ui->sourceInput->text());

	if (isTexture(source_info))
	{
		importTexture();
		return;
	}
	else
	{
		importModel();
		if (m_ui->importAnimationCheckbox->isChecked())
		{
			importAnimation();
		}
		return;
	}
	Q_ASSERT(false);
	on_progressUpdate(1, "Error.");
}


ImportAssetDialog::~ImportAssetDialog()
{
	delete m_ui;
	delete m_import_thread;
}