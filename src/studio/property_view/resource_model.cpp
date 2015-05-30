#include "resource_model.h"
#include "core/FS/file_system.h"
#include "core/json_serializer.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/texture.h"
#include <qapplication.h>
#include <qfile.h>
#include <qfiledialog.h>
#include <qlayout.h>
#include <qlineedit.h>
#include <qpainter.h>
#include <qpushbutton.h>


FileInput::FileInput(QWidget* parent)
	: QWidget(parent)
{
	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_edit = new QLineEdit(this);
	layout->addWidget(m_edit);
	QPushButton* browse_button = new QPushButton("...", this);
	layout->addWidget(browse_button);
	connect(browse_button, &QPushButton::clicked, this, &FileInput::browseClicked);
	connect(m_edit, &QLineEdit::editingFinished, this, &FileInput::editingFinished);
}


void FileInput::editingFinished()
{
	setValue(m_edit->text());
}


void FileInput::setValue(const QString& path)
{
	m_edit->setText(path);
	emit valueChanged();
}


QString FileInput::value() const 
{
	return m_edit->text();
}


void FileInput::browseClicked()
{
	auto path = QFileDialog::getOpenFileName(this);
	if (!path.isEmpty())
	{
		setValue(path);
	}
}


static QSize getPreviewSize(Lumix::Texture* texture)
{
	int w = texture->getWidth();
	int h = texture->getHeight();
	if (w > 150)
	{
		h *= 150.0f / w;
		w = 150;
	}
	return QSize(w, h);
}


ResourceModel::ResourceModel(Lumix::WorldEditor& editor, const Lumix::Path& path)
	: m_editor(editor)
{
	m_resource = nullptr;
	setResource(path);
}


void ResourceModel::setResource(const Lumix::Path& path)
{
	if (m_resource)
	{
		m_resource->getResourceManager().get(m_resource_type)->unload(*m_resource);
		m_resource->getObserverCb().unbind<ResourceModel, &ResourceModel::onResourceLoaded>(this);
	}

	char rel_path[LUMIX_MAX_PATH];
	m_editor.getRelativePath(rel_path, LUMIX_MAX_PATH, path);
	char extension[10];
	Lumix::PathUtils::getExtension(extension, sizeof(extension), path);
	if (strcmp(extension, "msh") == 0)
	{
		m_resource_type = Lumix::ResourceManager::MODEL;
	}
	else if (strcmp(extension, "mat") == 0)
	{
		m_resource_type = Lumix::ResourceManager::MATERIAL;
	}
	else if (strcmp(extension, "dds") == 0 || strcmp(extension, "tga") == 0 || strcmp(extension, "raw") == 0)
	{
		m_resource_type = Lumix::ResourceManager::TEXTURE;
	}
	else
	{
		m_resource_type = 0;
		m_resource = NULL;
		return;
	}

	Lumix::ResourceManagerBase* manager = m_editor.getEngine().getResourceManager().get(m_resource_type);
	m_resource = manager->load(Lumix::Path(rel_path));
	m_resource->getObserverCb().bind<ResourceModel, &ResourceModel::onResourceLoaded>(this);
	onResourceLoaded(m_resource->getState(), m_resource->getState());
}


ResourceModel::~ResourceModel()
{
	ASSERT(m_resource);
	m_resource->getResourceManager().get(m_resource_type)->unload(*m_resource);
	m_resource->getObserverCb().unbind<ResourceModel, &ResourceModel::onResourceLoaded>(this);
}


void ResourceModel::fillModelInfo()
{
	Lumix::Model* model = static_cast<Lumix::Model*>(m_resource);
	auto object = this->object("Model", model);
	object
		.property("Bone count", &Lumix::Model::getBoneCount)
		.property("Bounding radius", &Lumix::Model::getBoundingRadius);
	auto meshes = object.array("Meshes", model->getMeshCount(), &Lumix::Model::getMeshPtr, [](const Lumix::Mesh* mesh) -> const char* { return mesh->getName(); });
		meshes.forEach([this](int, const Lumix::Mesh* mesh, Node& node){
			Object<Lumix::Mesh>((Lumix::Mesh*)mesh, &node)
				.property("Triangles", &Lumix::Mesh::getTriangleCount);
			fillMaterialInfo(mesh->getMaterial(), node.addChild("material"));
		});
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


void ResourceModel::showFileDialog(const DynamicObjectModel::Node* node, QString filter)
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


class BaseEditorProperty
{
	public:
		BaseEditorProperty();
		virtual ~BaseEditorProperty();

		virtual int childCount() const { return m_children.size(); }
		virtual void addChild(BaseEditorProperty* child);
		virtual void removeChild(int index);
		virtual QVariant getValue() const = 0;
		virtual BaseEditorProperty* getParent() const { return m_parent; }

	private:
		BaseEditorProperty* m_parent;
		QVector<BaseEditorProperty*> m_children;
};


BaseEditorProperty::BaseEditorProperty()
	: m_parent(nullptr)
{}


BaseEditorProperty::~BaseEditorProperty()
{
	for (auto child : m_children)
	{
		delete child;
	}
}


void BaseEditorProperty::addChild(BaseEditorProperty* child)
{
	child->m_parent = this;
	m_children.append(child);
}


void BaseEditorProperty::removeChild(int index)
{
	delete m_children[index];
	m_children.removeAt(index);
}


void ResourceModel::fillMaterialInfo(Lumix::Material* material, Node& node)
{
	auto object = Object<Lumix::Material>(material, &node);
	node.m_getter = [material]() -> QVariant { return material->getPath().c_str(); };
	node.m_name = "Material";
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
		.property("Normal mapping", &Lumix::Material::isNormalMapping, &Lumix::Material::enableNormalMapping)
		.property("Shadow receiver", &Lumix::Material::isShadowReceiver, &Lumix::Material::enableShadowReceiving)
		.property("Z test", &Lumix::Material::isZTest, &Lumix::Material::enableZTest)
		.property("Shader", 
			[](Lumix::Material* material) -> QVariant { return material->getShader() ? material->getShader()->getPath().c_str() : ""; },
			[this](Lumix::Material* material, QVariant value) { setMaterialShader(material, value.toString()); }
		);
	auto shader_node = object.getNode().m_children.back();
	shader_node->onClick = [shader_node, this](QWidget*, QPoint) { showFileDialog(shader_node, "Shaders (*.shd)"); };

	for (int i = 0; i < material->getUniformCount(); ++i)
	{
		auto& uniform = material->getUniform(i);
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
	
	object
		.array("Textures", material->getTextureCount(), &Lumix::Material::getTexture, 
			[](Lumix::Texture* texture) -> const char* { return texture->getPath().c_str(); }
		)
			.forEach([this, material](int i, Lumix::Texture* texture, Node& node) {
				fillTextureInfo(texture, node);
				node.m_is_persistent_editor = true;
				Object<Lumix::Texture>(texture, &node)
					.property("uniform"
						, [material, i](Lumix::Texture*) -> QVariant { return material->getTextureUniform(i); }
						, [material, i](Lumix::Texture*, QVariant value) { material->setTextureUniform(i, value.toString().toLatin1().data()); }
					);
				node.m_name = QString("Texture %1").arg(i);
				node.onCreateEditor = [&node, i, texture, material](QWidget* parent, const QStyleOptionViewItem&) -> QWidget* {
					auto input = new FileInput(parent);	
					input->setValue(texture->getPath().c_str());
					input->connect(input, &FileInput::valueChanged, [&node, input]() {
						node.m_setter(input->value());
					});
					return input;
				};
				node.m_setter = [material, i](const QVariant& value) {
					if (value.isValid())
					{
						material->setTexturePath(i, Lumix::Path(value.toString().toLatin1().data()));
					}
				};
				//node.onClick = [node, this](QWidget*, QPoint) { showFileDialog(&node, "Textures (*.dds|*.tga)"); };
			}
			, [material]() {
				auto texture = static_cast<Lumix::Texture*>(material->getResourceManager().get(Lumix::ResourceManager::TEXTURE)->load(Lumix::Path("texture.dds")));
				material->addTexture(texture);	
				return false;
			}
		);
}


void ResourceModel::fillTextureInfo(Lumix::Texture* texture, Node& node)
{
	node.m_name = "Texture";
	auto obj = Object<Lumix::Texture>(texture, &node)
		.property("Width", &Lumix::Texture::getWidth)
		.property("Height", &Lumix::Texture::getHeight)
		.property("Bytes per pixel", &Lumix::Texture::getBytesPerPixel);
	auto& preview = obj.getNode().addChild("Preview");
	preview.m_getter = []() -> QVariant {
		return "";
	};
	preview.m_decoration = [texture]() -> QVariant {
		QSize size = getPreviewSize(texture);
		return QImage(texture->getPath().c_str()).scaled(size);
	};
	preview.m_size_hint = [texture]() -> QVariant {
		return getPreviewSize(texture);
	};
}


void ResourceModel::onResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state)
{
	beginResetModel();
	/* 
		endResetModel closes any opened property editor, if this is done in the end of this method
		it can result in a crash, since the editor can access some destroyed node
	*/
	endResetModel(); 
	beginResetModel();
	for (int i = 0; i < getRoot().m_children.size(); ++i)
	{
		delete getRoot().m_children[i];
	}
	getRoot().m_children.clear();
	this->getRoot().m_getter = [new_state]() -> QVariant { return new_state == Lumix::Resource::State::LOADING ? "Loading..." : "Ready"; };
	if (new_state == Lumix::Resource::State::READY || new_state == Lumix::Resource::State::FAILURE)
	{
		if (dynamic_cast<Lumix::Model*>(m_resource))
		{
			fillModelInfo();
		}
		else if (dynamic_cast<Lumix::Material*>(m_resource))
		{
			fillMaterialInfo(static_cast<Lumix::Material*>(m_resource), getRoot());
		}
		else if (dynamic_cast<Lumix::Texture*>(m_resource))
		{
			fillTextureInfo(static_cast<Lumix::Texture*>(m_resource), getRoot());
		}
		else
		{
			Q_ASSERT(false);
		}
	}
	endResetModel();
	if (new_state == Lumix::Resource::State::READY || new_state == Lumix::Resource::State::FAILURE)
	{
		emit modelReady();
	}
}
