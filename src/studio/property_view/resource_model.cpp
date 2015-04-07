#include "resource_model.h"
#include "core/FS/file_system.h"
#include "core/json_serializer.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/texture.h"
#include <qapplication.h>
#include <qfile.h>
#include <qfiledialog.h>
#include <qpainter.h>


ResourceModel::ResourceModel(Lumix::WorldEditor& editor, Lumix::Resource* resource)
	: m_resource(resource)
	, m_editor(editor)
{
	m_resource->getObserverCb().bind<ResourceModel, &ResourceModel::onResourceLoaded>(this);
	if (resource->isReady())
	{
		onResourceLoaded(Lumix::Resource::State::READY, Lumix::Resource::State::READY);
	}
}


ResourceModel::~ResourceModel()
{
	m_resource->getObserverCb().unbind<ResourceModel, &ResourceModel::onResourceLoaded>(this);
}


void ResourceModel::fillModelInfo()
{
	Lumix::Model* model = static_cast<Lumix::Model*>(m_resource);
	auto object = this->object("Model", model);
	object
		.property("Bone count", &Lumix::Model::getBoneCount)
		.property("Bounding radius", &Lumix::Model::getBoundingRadius)
		.array("Meshes", model->getMeshCount(), &Lumix::Model::getMeshPtr, [](const Lumix::Mesh* mesh) -> const char* { return mesh->getName(); })
		.property("Triangles", &Lumix::Mesh::getTriangleCount)
		.property("Material", [](const Lumix::Mesh* mesh) -> const char* { return mesh->getMaterial()->getPath().c_str(); });
}


static Lumix::Material::Uniform* getMaterialUniform(Lumix::Material* material, QString name)
{
	for (int i = 0; i < material->getUniformCount(); ++i)
	{
		if (name == material->getUniform(i).m_name)
		{
			return &material->getUniform(i);
		}
	}
	return NULL;
}


void ResourceModel::saveMaterial(Lumix::Material* material)
{
	Lumix::FS::FileSystem& fs = m_editor.getEngine().getFileSystem();
	// use temporary because otherwise the material is reloaded during saving
	char tmp_path[LUMIX_MAX_PATH];
	strcpy(tmp_path, material->getPath().c_str());
	strcat(tmp_path, ".tmp");
	Lumix::FS::IFile* file = fs.open(fs.getDefaultDevice(), tmp_path, Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
	if (file)
	{
		Lumix::DefaultAllocator allocator;
		Lumix::JsonSerializer serializer(*file, Lumix::JsonSerializer::AccessMode::WRITE, material->getPath().c_str(), allocator);
		material->save(serializer);
		fs.close(file);

		QFile::remove(material->getPath().c_str());
		QFile::rename(tmp_path, material->getPath().c_str());
	}
	else
	{
		Lumix::g_log_error.log("Material manager") << "Could not save file " << material->getPath().c_str();
	}
}


void ResourceModel::showFileDialog(DynamicObjectModel::Node* node, QString filter)
{
	auto fileName = QFileDialog::getOpenFileName(NULL, "Select file", "", filter);
	if (!fileName.isEmpty())
	{
		node->m_setter(fileName);
	}
}


void ResourceModel::setMaterialShader(Lumix::Material* material, QString value)
{
	char rel_path[LUMIX_MAX_PATH];
	m_editor.getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(value.toLatin1().data()));
	Lumix::StackAllocator<LUMIX_MAX_PATH> allocator;
	material->setShader(Lumix::Path(rel_path));
}


void ResourceModel::fillMaterialInfo()
{
	Lumix::Material* material = static_cast<Lumix::Material*>(m_resource);
	auto object = this->object("Material", material);
	object.getNode().onClick = [this, material](QWidget*, QPoint) { saveMaterial(material); };
	object.getNode().onPaint = [](QPainter* painter, const QStyleOptionViewItem& option) {
		painter->save();
		QStyleOptionButton button_style_option;
		button_style_option.rect = option.rect;
		button_style_option.text = "Save";
		QApplication::style()->drawControl(QStyle::CE_PushButton, &button_style_option, painter);
		painter->restore();
	};
	object
		.property("Alpha cutout", &Lumix::Material::isAlphaCutout, &Lumix::Material::enableAlphaCutout)
		.property("Alpha to coverage", &Lumix::Material::isAlphaToCoverage, &Lumix::Material::enableAlphaToCoverage)
		.property("Backface culling", &Lumix::Material::isBackfaceCulling, &Lumix::Material::enableBackfaceCulling)
		.property("Shadow receiver", &Lumix::Material::isShadowReceiver, &Lumix::Material::enableShadowReceiving)
		.property("Z test", &Lumix::Material::isZTest, &Lumix::Material::enableZTest)
		.property("Shader", 
			[](Lumix::Material* material) -> QVariant { return material->getShader()->getPath().c_str(); },
			[this](Lumix::Material* material, QVariant value) { setMaterialShader(material, value.toString()); }
		);
	auto shader_node = object.getNode().m_children.back();
	object.getNode().m_children.back()->onClick = [shader_node, this](QWidget*, QPoint) { showFileDialog(shader_node, "Shaders (*.shd)"); };

	object
		.array("Textures", material->getTextureCount(), &Lumix::Material::getTexture, [](Lumix::Texture* texture) -> const char* { return texture->getPath().c_str(); })
			.property("Width", &Lumix::Texture::getWidth)
			.property("Height", &Lumix::Texture::getHeight)
			.property("Bytes per pixel", &Lumix::Texture::getBytesPerPixel);
	for (int i = 0; i < material->getUniformCount(); ++i)
	{
		auto& uniform = material->getUniform(i);
		if (uniform.m_is_editable)
		{
			QString name = uniform.m_name;
			object.property(uniform.m_name
				, [name](Lumix::Material* material) -> QVariant {
					Lumix::Material::Uniform* uniform = getMaterialUniform(material, name);
					if (uniform)
					{
						switch (uniform->m_type)
						{
							case Lumix::Material::Uniform::FLOAT:
								return uniform->m_float;
						}
					}
					return QVariant();
				}
				, [name](Lumix::Material* material, const QVariant& value) {
					Lumix::Material::Uniform* uniform = getMaterialUniform(material, name);
					if (uniform)
					{
						switch (uniform->m_type)
						{
							case Lumix::Material::Uniform::FLOAT:
								uniform->m_float = value.toFloat();
								break;
						}
					}
				}
			);
		}
	}
}


void ResourceModel::fillTextureInfo()
{
	Lumix::Texture* texture = static_cast<Lumix::Texture*>(m_resource);
	object("Texture", texture)
		.property("Width", &Lumix::Texture::getWidth)
		.property("Height", &Lumix::Texture::getHeight)
		.property("Bytes per pixel", &Lumix::Texture::getBytesPerPixel);
}


void ResourceModel::onResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state)
{
	if (new_state == Lumix::Resource::State::READY)
	{
		beginResetModel();
		for (int i = 0; i < getRoot().m_children.size(); ++i)
		{
			delete getRoot().m_children[i];
		}
		getRoot().m_children.clear();
		if (dynamic_cast<Lumix::Model*>(m_resource))
		{
			fillModelInfo();
		}
		else if (dynamic_cast<Lumix::Material*>(m_resource))
		{
			fillMaterialInfo();
		}
		else if (dynamic_cast<Lumix::Texture*>(m_resource))
		{
			fillTextureInfo();
		}
		else
		{
			Q_ASSERT(false);
		}
		endResetModel();
	}
}
