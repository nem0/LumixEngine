#include "property_view.h"
#include "ui_property_view.h"
#include "animation/animation_system.h"
#include "assetbrowser.h"
#include "core/aabb.h"
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "entity_list.h"
#include "entity_template_list.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/render_scene.h"
#include "graphics/shader.h"
#include "graphics/texture.h"
#include "property_view/component_property_object.h"
#include "property_view/getter_setter_object.h"
#include "property_view/instance_object.h"
#include "property_view/terrain_editor.h"
#include "script/script_system.h"
#include "scripts/scriptcompiler.h"
#include <qcheckbox.h>
#include <QColorDialog>
#include <qdesktopservices.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <qfiledialog.h>
#include <qmenu.h>
#include <qmessagebox.h>
#include <qmimedata.h>
#include <qlineedit.h>
#include <qpushbutton.h>
#include <qtextstream.h>


static const char* component_map[] =
{
	"Animable", "animable",
	"Camera", "camera",
	"Directional light", "light",
	"Mesh", "renderable",
	"Physics Box", "box_rigid_actor",
	"Physics Controller", "physical_controller",
	"Physics Mesh", "mesh_rigid_actor",
	"Physics Heightfield", "physical_heightfield",
	"Script", "script",
	"Terrain", "terrain"
};


static const uint32_t TERRAIN_HASH = crc32("terrain");
static const uint32_t SCRIPT_HASH = crc32("script");


void createEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Material::Uniform, false>* uniform)
{
	switch (uniform->getValue()->m_type)
	{
		case Lumix::Material::Uniform::FLOAT:
			{
				QDoubleSpinBox* spinbox = new QDoubleSpinBox();
				spinbox->setValue(uniform->getValue()->m_float);
				item->treeWidget()->setItemWidget(item, 1, spinbox);
				spinbox->setMaximum(FLT_MAX);
				spinbox->setMinimum(-FLT_MAX);
				spinbox->connect(spinbox, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [uniform](double new_value)
				{
					uniform->getValue()->m_float = (float)new_value;
				});
			}
			break;
		default:
			ASSERT(false);
			break;
	}
	
}


void createEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Texture, false>* texture)
{
	item->setText(1, texture->getValue()->getPath().c_str());
}


void createEditor(PropertyView& view, QTreeWidgetItem* item, InstanceObject<Lumix::Shader, false>* shader)
{
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	FileEdit* edit = new FileEdit(NULL, &view);
	edit->setText(shader->getValue()->getPath().c_str());
	layout->addWidget(edit);
	layout->setContentsMargins(0, 0, 0, 0);
	item->treeWidget()->setItemWidget(item, 1, widget);
	edit->connect(edit, &FileEdit::editingFinished, [shader, edit]()
	{
		auto material = static_cast<InstanceObject<Lumix::Material, false>* >(shader->getParent())->getValue();
		auto shader = static_cast<Lumix::Shader*>(material->getResourceManager().get(Lumix::ResourceManager::SHADER)->load(Lumix::Path(edit->text().toLatin1().data())));
		material->setShader(shader);
	});
	QPushButton* button = new QPushButton("...");
	layout->addWidget(button);
	button->connect(button, &QPushButton::clicked, [&view, shader, edit]()
	{
		QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "Shader (*.shd)");
		if(str != "")
		{
			char rel_path[LUMIX_MAX_PATH];
			QByteArray byte_array = str.toLatin1();
			const char* text = byte_array.data();
			view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));

			auto material = static_cast<InstanceObject<Lumix::Material, false>* >(shader->getParent())->getValue();
			auto shader = static_cast<Lumix::Shader*>(material->getResourceManager().get(Lumix::ResourceManager::SHADER)->load(Lumix::Path(rel_path)));
			material->setShader(shader);

			edit->setText(rel_path);
		}
	});

	QPushButton* go_button = new QPushButton("->");
	layout->addWidget(go_button);
	go_button->connect(go_button, &QPushButton::clicked, [edit]()
	{
		QDesktopServices::openUrl(QUrl::fromLocalFile(edit->text()));
	});
}


void createEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Model, false>* model)
{
	item->setText(1, model->getValue()->getPath().c_str());
}


void createImageEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Texture, false>* texture)
{
	QLabel* image_label = new QLabel();
	item->treeWidget()->setItemWidget(item, 1, image_label);
	QImage image(texture->getValue()->getPath().c_str());
	image_label->setPixmap(QPixmap::fromImage(image).scaledToHeight(100));
	image_label->adjustSize();
}


void createEditor(PropertyView& view, QTreeWidgetItem* item, InstanceObject<Lumix::Material, false>* material)
{
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QLabel* label = new QLabel(material->getValue()->getPath().c_str());
	layout->addWidget(label);
	QPushButton* button = new QPushButton("Save");
	QPushButton* go_button = new QPushButton("->");
	layout->addWidget(button);
	layout->addWidget(go_button);
	go_button->connect(go_button, &QPushButton::clicked, [material]()
	{
		QDesktopServices::openUrl(QUrl::fromLocalFile(material->getValue()->getPath().c_str()));
	});

	button->connect(button, &QPushButton::clicked, [material, &view]()
	{
		Lumix::FS::FileSystem& fs = view.getWorldEditor()->getEngine().getFileSystem();
		// use temporary because otherwise the material is reloaded during saving
		char tmp_path[LUMIX_MAX_PATH];
		strcpy(tmp_path, material->getValue()->getPath().c_str());
		strcat(tmp_path, ".tmp");
		Lumix::FS::IFile* file = fs.open(fs.getDefaultDevice(), tmp_path, Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
		if(file)
		{
			Lumix::DefaultAllocator allocator;
			Lumix::JsonSerializer serializer(*file, Lumix::JsonSerializer::AccessMode::WRITE, material->getValue()->getPath().c_str(), allocator);
			material->getValue()->save(serializer);
			fs.close(file);

			QFile::remove(material->getValue()->getPath().c_str());
			QFile::rename(tmp_path, material->getValue()->getPath().c_str());
		}
		else
		{
			Lumix::g_log_error.log("Material manager") << "Could not save file " << material->getValue()->getPath().c_str();
		}
	});
	item->treeWidget()->setItemWidget(item, 1, widget);
}


void createEditor(PropertyView&, QTreeWidgetItem* item, InstanceObject<Lumix::Mesh, false>* mesh)
{
	item->setText(1, mesh->getValue()->getName());
}


void createComponentEditor(PropertyView& view, QTreeWidgetItem* item, InstanceObject<Lumix::Component, true>* component)
{
	if (component->getValue()->type == TERRAIN_HASH)
	{
		view.addTerrainCustomProperties(*item, *component->getValue());
	}
	else if (component->getValue()->type == SCRIPT_HASH)
	{
		view.addScriptCustomProperties(*item, *component->getValue());
	}
}


void createTextureInMaterialEditor(PropertyView& view, QTreeWidgetItem* item, InstanceObject<Lumix::Texture, false>* texture)
{
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QLineEdit* edit = new QLineEdit(texture->getValue()->getPath().c_str());
	layout->addWidget(edit);
	edit->connect(edit, &QLineEdit::editingFinished, [&view, edit, texture]()
	{
		char rel_path[LUMIX_MAX_PATH];
		QByteArray byte_array = edit->text().toLatin1();
		const char* text = byte_array.data();
		view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));
		auto material = static_cast<InstanceObject<Lumix::Material, false>* >(texture->getParent())->getValue();
		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			if (material->getTexture(i) == texture->getValue())
			{
				Lumix::Texture* new_texture = static_cast<Lumix::Texture*>(material->getResourceManager().get(Lumix::ResourceManager::TEXTURE)->load(Lumix::Path(rel_path)));
				material->setTexture(i, new_texture);
				break;
			}
		}
	});

	QPushButton* browse_button = new QPushButton("...");
	layout->addWidget(browse_button);
	browse_button->connect(browse_button, &QPushButton::clicked, [&view, edit, texture]()
	{
		QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "Texture (*.tga; *.dds)");
		if (str != "")
		{
			char rel_path[LUMIX_MAX_PATH];
			QByteArray byte_array = str.toLatin1();
			const char* text = byte_array.data();
			view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));
			auto material = static_cast<InstanceObject<Lumix::Material, false>* >(texture->getParent())->getValue();
			for (int i = 0; i < material->getTextureCount(); ++i)
			{
				if (material->getTexture(i) == texture->getValue())
				{
					Lumix::Texture* new_texture = static_cast<Lumix::Texture*>(material->getResourceManager().get(Lumix::ResourceManager::TEXTURE)->load(Lumix::Path(rel_path)));
					material->setTexture(i, new_texture);
					break;
				}
			}
			edit->setText(rel_path);
		}
	});

	QPushButton* remove_button = new QPushButton(" - ");
	layout->addWidget(remove_button);
	remove_button->connect(remove_button, &QPushButton::clicked, [texture, &view, item]()
	{
		auto material = static_cast<InstanceObject<Lumix::Material, false>* >(texture->getParent())->getValue();
		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			if (material->getTexture(i) == texture->getValue())
			{
				material->removeTexture(i);
				item->parent()->removeChild(item);
				break;
			}
		}
	});

	QPushButton* add_button = new QPushButton(" + ");
	layout->addWidget(add_button);
	add_button->connect(add_button, &QPushButton::clicked, [texture, &view, item]()
	{
		auto material = static_cast<InstanceObject<Lumix::Material, false>* >(texture->getParent())->getValue();
		Lumix::Texture* new_texture = static_cast<Lumix::Texture*>(material->getResourceManager().get(Lumix::ResourceManager::TEXTURE)->load(Lumix::Path("models/editor/default.tga")));
		material->addTexture(new_texture);
	});

	item->treeWidget()->setItemWidget(item, 1, widget);
}


PropertyViewObject* createComponentObject(PropertyViewObject* parent, Lumix::WorldEditor& editor, Lumix::Component cmp)
{
	const char* name = "";
	for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		if (crc32(component_map[i + 1]) == cmp.type)
		{
			name = component_map[i];
		}
	}
	auto c = new Lumix::Component(cmp);
	InstanceObject<Lumix::Component, true>* object = new InstanceObject<Lumix::Component, true>(parent, name, c, &createComponentEditor);
	
	auto& descriptors = editor.getPropertyDescriptors(cmp.type);
	
	for (int i = 0; i < descriptors.size(); ++i)
	{
		auto prop = new ComponentPropertyObject(object, descriptors[i]->getName(), cmp, *descriptors[i]);
		object->addMember(prop);
	}

	return object;
}


PropertyViewObject* createEntityObject(Lumix::WorldEditor& editor, Lumix::Entity entity)
{
	auto e = new Lumix::Entity(entity);
	InstanceObject<Lumix::Entity, true>* object = new InstanceObject<Lumix::Entity, true>(NULL, "Entity", e, NULL);

	auto& cmps = editor.getComponents(*e);

	for (int i = 0; i < cmps.size(); ++i)
	{
		auto prop = createComponentObject(object, editor, cmps[i]);
		object->addMember(prop);
	}

	return object;
}


PropertyViewObject* createTextureObject(PropertyViewObject* parent, Lumix::Resource* resource)
{
	if (Lumix::Texture* texture = dynamic_cast<Lumix::Texture*>(resource))
	{
		auto object = new InstanceObject<Lumix::Texture, false>(parent, "Texture", texture, &createEditor);

		auto prop = new GetterSetterObject<int, Lumix::Texture>(object, "width", texture, &Lumix::Texture::getWidth, NULL, &createEditor);
		object->addMember(prop);
		
		prop = new GetterSetterObject<int, Lumix::Texture>(object, "height", texture, &Lumix::Texture::getHeight, NULL, &createEditor);
		object->addMember(prop);

		auto img_object = new InstanceObject<Lumix::Texture, false>(object, "Image", texture, &createImageEditor);
		object->addMember(img_object);

		return object;
	}
	return NULL;
}


static PropertyViewObject* createMaterialObject(PropertyViewObject* parent, Lumix::Resource* resource)
{
	if (Lumix::Material* material = dynamic_cast<Lumix::Material*>(resource))
	{
		auto object = new InstanceObject<Lumix::Material, false>(parent, "Material", material, &createEditor);

		PropertyViewObject* prop = new InstanceObject<Lumix::Shader, false>(object, "Shader", material->getShader(), &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Z test", material, &Lumix::Material::isZTest, &Lumix::Material::enableZTest, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Backface culling", material, &Lumix::Material::isBackfaceCulling, &Lumix::Material::enableBackfaceCulling, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Alpha to coverage", material, &Lumix::Material::isAlphaToCoverage, &Lumix::Material::enableAlphaToCoverage, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Shadow receiver", material, &Lumix::Material::isShadowReceiver, &Lumix::Material::enableShadowReceiving, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<bool, Lumix::Material>(object, "Alpha cutout", material, &Lumix::Material::isAlphaCutout, &Lumix::Material::enableAlphaCutout, &createEditor);
		object->addMember(prop);

		for (int i = 0; i < material->getTextureCount(); ++i)
		{
			prop = createTextureObject(object, material->getTexture(i));
			static_cast<InstanceObject<Lumix::Texture, false>*>(prop)->setEditor(createTextureInMaterialEditor) ;
			object->addMember(prop);
		}

		for (int i = 0; i < material->getUniformCount(); ++i)
		{
			Lumix::Material::Uniform& uniform = material->getUniform(i);
			if (uniform.m_is_editable)
			{
				prop = new InstanceObject<Lumix::Material::Uniform, false>(object, uniform.m_name, &uniform, createEditor);
				object->addMember(prop);
			}
		}
		return object;
	}
	return NULL;
}


PropertyViewObject* createModelObject(PropertyViewObject* parent, Lumix::Resource* resource)
{
	if (Lumix::Model* model = dynamic_cast<Lumix::Model*>(resource))
	{
		auto* object = new InstanceObject<Lumix::Model, false>(parent, "Model", model, &createEditor);

		PropertyViewObject* prop = new GetterSetterObject<int, Lumix::Model>(object, "Bone count", model, &Lumix::Model::getBoneCount, NULL, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<float, Lumix::Model>(object, "Bounding radius", model, &Lumix::Model::getBoundingRadius, NULL, &createEditor);
		object->addMember(prop);

		prop = new GetterSetterObject<size_t, Lumix::Model>(object, "Size (bytes)", model, &Lumix::Model::size, NULL, &createEditor);
		object->addMember(prop);

		for (int i = 0; i < model->getMeshCount(); ++i)
		{
			Lumix::Mesh* mesh = &model->getMesh(i);
			auto mesh_object = new InstanceObject<Lumix::Mesh, false>(object, "Mesh", mesh, &createEditor);
			object->addMember(mesh_object);

			prop = new GetterSetterObject<int, Lumix::Mesh>(mesh_object, "Triangles", mesh, &Lumix::Mesh::getTriangleCount, NULL, &createEditor);
			mesh_object->addMember(prop);

			prop = createMaterialObject(mesh_object, mesh->getMaterial());
			mesh_object->addMember(prop);
		}

		return object;
	}
	return NULL;
}


PropertyViewObject::~PropertyViewObject()
{
	for (int i = 0; i < m_members.size(); ++i)
	{
		delete m_members[i];
	}
}


PropertyView::PropertyView(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::PropertyView)
	, m_terrain_editor(NULL)
	, m_selected_resource(NULL)
	, m_object(NULL)
	, m_selected_entity(Lumix::Entity::INVALID)
{
	m_ui->setupUi(this);

	QStringList component_list;
	for(int j = 0; j < sizeof(component_map) / sizeof(component_map[0]); j += 2)
	{
		component_list << component_map[j];
	}
	m_ui->componentTypeCombo->insertItems(0, component_list);

	addResourcePlugin(&createMaterialObject);
	addResourcePlugin(&createModelObject);
	addResourcePlugin(&createTextureObject);
}


PropertyView::~PropertyView()
{
	m_world_editor->entitySelected().unbind<PropertyView, &PropertyView::onEntitySelected>(this);
	m_world_editor->universeCreated().unbind<PropertyView, &PropertyView::onUniverseCreated>(this);
	m_world_editor->universeDestroyed().unbind<PropertyView, &PropertyView::onUniverseDestroyed>(this);
	onUniverseCreated();
	delete m_terrain_editor;
	delete m_ui;
}


void PropertyView::onEntityPosition(const Lumix::Entity& e)
{
	if (m_selected_entity == e)
	{
		bool b1 = m_ui->positionX->blockSignals(true);
		bool b2 = m_ui->positionY->blockSignals(true);
		bool b3 = m_ui->positionZ->blockSignals(true);
		
		Lumix::Vec3 pos = e.getPosition();
		m_ui->positionX->setValue(pos.x);
		m_ui->positionY->setValue(pos.y);
		m_ui->positionZ->setValue(pos.z);
		
		m_ui->positionX->blockSignals(b1);
		m_ui->positionY->blockSignals(b2);
		m_ui->positionZ->blockSignals(b3);
	}
}


void PropertyView::refresh()
{
	setObject(NULL);
	onEntitySelected(getWorldEditor()->getSelectedEntities());
}


Lumix::WorldEditor* PropertyView::getWorldEditor()
{
	return m_world_editor;
}


void PropertyView::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_world_editor = &editor;
	m_terrain_editor = new TerrainEditor(editor, m_entity_template_list, m_entity_list);
	m_world_editor->addPlugin(m_terrain_editor);
	m_world_editor->entitySelected().bind<PropertyView, &PropertyView::onEntitySelected>(this);
	m_world_editor->universeCreated().bind<PropertyView, &PropertyView::onUniverseCreated>(this);
	m_world_editor->universeDestroyed().bind<PropertyView, &PropertyView::onUniverseDestroyed>(this);
	if (m_world_editor->getEngine().getUniverse())
	{
		onUniverseCreated();
	}
}


void PropertyView::onUniverseCreated()
{
	m_world_editor->getEngine().getUniverse()->entityMoved().bind<PropertyView, &PropertyView::onEntityPosition>(this);
}


void PropertyView::onUniverseDestroyed()
{
	m_world_editor->getEngine().getUniverse()->entityMoved().unbind<PropertyView, &PropertyView::onEntityPosition>(this);
}


void PropertyView::setAssetBrowser(AssetBrowser& asset_browser)
{
	m_asset_browser = &asset_browser;
	connect(m_asset_browser, &AssetBrowser::fileSelected, this, &PropertyView::setSelectedResourceFilename);
}


void PropertyView::setSelectedResourceFilename(const char* filename)
{
	char rel_path[LUMIX_MAX_PATH];
	m_world_editor->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(filename));
	Lumix::ResourceManagerBase* manager = NULL;
	char extension[10];
	Lumix::PathUtils::getExtension(extension, sizeof(extension), filename);
	if (strcmp(extension, "msh") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::MODEL);
	}
	else if (strcmp(extension, "mat") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL);
	}
	else if (strcmp(extension, "dds") == 0 || strcmp(extension, "tga") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::TEXTURE);
	}

	if (manager != NULL)
	{
		setSelectedResource(manager->load(Lumix::Path(rel_path)));
	}
	else
	{
		setSelectedResource(NULL);
	}
}


void PropertyView::addResourcePlugin(PropertyViewObject::Creator plugin)
{
	m_resource_plugins.push_back(plugin);
}


void PropertyView::onSelectedResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state)
{
	if (new_state == Lumix::Resource::State::READY)
	{
		m_selected_entity = Lumix::Entity::INVALID;
		clear();
		for (int i = 0; i < m_resource_plugins.size(); ++i)
		{
			if (PropertyViewObject* object = m_resource_plugins[i](NULL, m_selected_resource))
			{
				setObject(object);
				return;
			}
		}
	}
}


void PropertyView::setScriptCompiler(ScriptCompiler* compiler)
{
	m_compiler = compiler;
	if(m_compiler)
	{
		connect(m_compiler, &ScriptCompiler::compiled, this, &PropertyView::on_script_compiled);
	}
}


void PropertyView::clear()
{
	m_ui->propertyList->clear();

	delete m_object;
	m_object = NULL;
}


void PropertyView::setScriptStatus(uint32_t status)
{
	for(int i = 0; i < m_ui->propertyList->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem* top_level_item = m_ui->propertyList->topLevelItem(i);
		for (int k = 0; k < top_level_item->childCount(); ++k)
		{
			QTreeWidgetItem* item = m_ui->propertyList->topLevelItem(i)->child(k);
			if (item->text(0) == "Script")
			{
				for (int j = 0; j < item->childCount(); ++j)
				{
					if (item->child(j)->text(0) == "Status")
					{
						switch (status)
						{
						case ScriptCompiler::SUCCESS:
							item->child(j)->setText(1, "Success");
							break;
						case ScriptCompiler::NOT_COMPILED:
							item->child(j)->setText(1, "Not compiled");
							break;
						case ScriptCompiler::UNKNOWN:
							item->child(j)->setText(1, "Unknown");
							break;
						case ScriptCompiler::FAILURE:
							item->child(j)->setText(1, "Failure");
							break;
						default:
							ASSERT(false);
							break;
						}

						return;
					}
				}
			}
		}
	}
}


void PropertyView::on_script_compiled(const Lumix::Path&, uint32_t status)
{
	setScriptStatus(status == 0 ? ScriptCompiler::SUCCESS : ScriptCompiler::FAILURE);
}


void PropertyView::addScriptCustomProperties(QTreeWidgetItem& tree_item, const Lumix::Component& script_component)
{
	QTreeWidgetItem* tools_item = new QTreeWidgetItem(QStringList() << "Tools");
	tree_item.insertChild(0, tools_item);
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* compile_button = new QPushButton("Compile", widget);
	layout->addWidget(compile_button);
	m_ui->propertyList->setItemWidget(tools_item, 1, widget);
	connect(compile_button, &QPushButton::clicked, this, [this, script_component](){
		Lumix::string path(m_world_editor->getAllocator());
		static_cast<Lumix::ScriptScene*>(script_component.scene)->getScriptPath(script_component, path);
		m_compiler->compile(Lumix::Path(path.c_str()));
	});

	QTreeWidgetItem* status_item = new QTreeWidgetItem(QStringList() << "Status");
	tree_item.insertChild(0, status_item);
	Lumix::string path(m_world_editor->getAllocator());
	static_cast<Lumix::ScriptScene*>(script_component.scene)->getScriptPath(script_component, path);
	switch (m_compiler->getStatus(Lumix::Path(path.c_str())))
	{
		case ScriptCompiler::SUCCESS:
			status_item->setText(1, "Compiled");
			break;
		case ScriptCompiler::FAILURE:
			status_item->setText(1, "Failure");
			break;
		default:
			status_item->setText(1, "Unknown");
			break;
	}
}


void PropertyView::addTerrainCustomProperties(QTreeWidgetItem& tree_item, const Lumix::Component& terrain_component)
{
	m_terrain_editor->m_tree_top_level = &tree_item;
	m_terrain_editor->m_component = terrain_component;

	{
		QWidget* widget = new QWidget();
		QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Save");
		tree_item.insertChild(0, item);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		QPushButton* height_button = new QPushButton("Heightmap", widget);
		layout->addWidget(height_button);
		QPushButton* texture_button = new QPushButton("Splatmap", widget);
		layout->addWidget(texture_button);
		layout->setContentsMargins(2, 2, 2, 2);
		m_ui->propertyList->setItemWidget(item, 1, widget);
		connect(height_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform("hm_texture")->save();
		});
		connect(texture_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform("splat_texture")->save();
		});
	}

	QSlider* slider = new QSlider(Qt::Orientation::Horizontal);
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Brush size");
	tree_item.insertChild(1, item);
	m_ui->propertyList->setItemWidget(item, 1, slider);
	slider->setMinimum(1);
	slider->setMaximum(100);
	connect(slider, &QSlider::valueChanged, [this](int value)
	{
		m_terrain_editor->m_terrain_brush_size = value;
	});

	slider = new QSlider(Qt::Orientation::Horizontal);
	item = new QTreeWidgetItem(QStringList() << "Brush strength");
	tree_item.insertChild(2, item);
	m_ui->propertyList->setItemWidget(item, 1, slider);
	slider->setMinimum(-100);
	slider->setMaximum(100);
	connect(slider, &QSlider::valueChanged, [this](int value)
	{
		m_terrain_editor->m_terrain_brush_strength = value / 100.0f;
	});

	QWidget* widget = new QWidget();
	item = new QTreeWidgetItem(QStringList() << "Brush type");
	tree_item.insertChild(3, item);
	QHBoxLayout* layout = new QHBoxLayout(widget);
	QPushButton* height_button = new QPushButton("Height", widget);
	layout->addWidget(height_button);
	QPushButton* texture_button = new QPushButton("Texture", widget);
	layout->addWidget(texture_button);
	QPushButton* entity_button = new QPushButton("Entity", widget);
	layout->addWidget(entity_button);
	layout->setContentsMargins(2, 2, 2, 2);
	m_ui->propertyList->setItemWidget(item, 1, widget);
	m_terrain_editor->m_type = TerrainEditor::HEIGHT;
	connect(height_button, &QPushButton::clicked, [this]()
	{
		m_terrain_editor->m_type = TerrainEditor::HEIGHT;
		if (m_terrain_editor->m_texture_tree_item)
		{
			m_terrain_editor->m_tree_top_level->removeChild(m_terrain_editor->m_texture_tree_item);
		}
	});
	connect(texture_button, &QPushButton::clicked, this, &PropertyView::on_TerrainTextureTypeClicked);
	connect(entity_button, &QPushButton::clicked, [this]()
	{
		m_terrain_editor->m_type = TerrainEditor::ENTITY;
		if (m_terrain_editor->m_texture_tree_item)
		{
			m_terrain_editor->m_tree_top_level->removeChild(m_terrain_editor->m_texture_tree_item);
		}
	});

}


void PropertyView::on_TerrainTextureTypeClicked()
{
	m_terrain_editor->m_type = TerrainEditor::TEXTURE;

	QComboBox* combobox = new QComboBox();
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Texture");
	m_terrain_editor->m_tree_top_level->insertChild(4, item);
	Lumix::Material* material = m_terrain_editor->getMaterial();
	if (material && material->isReady())
	{
		for (int i = 1; i < material->getTextureCount() - 1; ++i)
		{
			combobox->addItem(material->getTexture(i)->getPath().c_str());
		}
	}
	m_ui->propertyList->setItemWidget(item, 1, combobox);

	m_terrain_editor->m_texture_tree_item = item;
	connect(combobox, (void (QComboBox::*)(int))&QComboBox::currentIndexChanged, this, &PropertyView::on_terrainBrushTextureChanged);
}


void PropertyView::on_terrainBrushTextureChanged(int value)
{
	m_terrain_editor->m_texture_idx = value;
}


void PropertyView::setSelectedResource(Lumix::Resource* resource)
{
	if(resource)
	{
		m_world_editor->selectEntities(NULL, 0);
	}
	clear();
	if (m_selected_resource)
	{
		m_selected_resource->getObserverCb().unbind<PropertyView, &PropertyView::onSelectedResourceLoaded>(this);
	}
	m_selected_resource = resource;
	if (resource)
	{
		m_selected_resource->onLoaded<PropertyView, &PropertyView::onSelectedResourceLoaded>(this);
	}
}


void PropertyView::onEntitySelected(const Lumix::Array<Lumix::Entity>& e)
{
	setSelectedResource(NULL);
	m_selected_entity = e.empty() ? Lumix::Entity::INVALID : e[0];
	clear();
	if (e.size() == 1 && e[0].isValid())
	{
		setObject(createEntityObject(*m_world_editor, e[0]));
		m_ui->propertyList->expandAll();
		onEntityPosition(e[0]);
		m_ui->nameEdit->setText(e[0].getName());
	}
}


void PropertyView::on_addComponentButton_clicked()
{
	QByteArray s = m_ui->componentTypeCombo->currentText().toLocal8Bit();
	const char* c = s.data();

	for(int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		if(strcmp(c, component_map[i]) == 0)
		{
			m_world_editor->addComponent(crc32(component_map[i+1]));
			Lumix::Array<Lumix::Entity> tmp(m_world_editor->getAllocator());
			tmp.push(m_selected_entity);
			onEntitySelected(tmp);
			return;
		}
	}
	ASSERT(false); // unknown component type
}


void PropertyView::updateSelectedEntityPosition()
{
	if(m_world_editor->getSelectedEntities().size() == 1)
	{
		Lumix::Array<Lumix::Vec3> positions(m_world_editor->getAllocator());
		positions.push(Lumix::Vec3((float)m_ui->positionX->value(), (float)m_ui->positionY->value(), (float)m_ui->positionZ->value()));
		m_world_editor->setEntitiesPositions(m_world_editor->getSelectedEntities(), positions);
	}
}


void PropertyView::on_positionX_valueChanged(double)
{
	updateSelectedEntityPosition();
}


void PropertyView::on_positionY_valueChanged(double)
{
	updateSelectedEntityPosition();
}


void PropertyView::on_positionZ_valueChanged(double)
{
	updateSelectedEntityPosition();
}


void PropertyView::createObjectEditor(QTreeWidgetItem* item, PropertyViewObject* object)
{
	item->setText(0, object->getName());
	object->createEditor(*this, item);

	PropertyViewObject** properties = object->getMembers();
	for (int i = 0; i < object->getMemberCount(); ++i)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->insertChild(0, subitem);

		createObjectEditor(subitem, properties[i]);
	}
}


PropertyViewObject* PropertyView::getObject()
{
	return m_object;
}


void PropertyView::setObject(PropertyViewObject* object)
{
	if(object != m_object)
	{
		clear();
	}
	else
	{
		m_ui->propertyList->clear();
	}

	m_object = object;
	
	if(object)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		m_ui->propertyList->insertTopLevelItem(0, item);
		createObjectEditor(item, object);

		m_ui->propertyList->expandAll();
		m_ui->propertyList->resizeColumnToContents(0);
	}
}


void PropertyView::on_propertyList_customContextMenuRequested(const QPoint &pos)
{
	QMenu* menu = new QMenu("Item actions", NULL);
	const QModelIndex& index = m_ui->propertyList->indexAt(pos);
	if (index.isValid() && index.parent().isValid() && !index.parent().parent().isValid() && m_selected_entity.isValid())
	{
		QAction* remove_component_action = new QAction("Remove component", menu);
		menu->addAction(remove_component_action);
		QAction* action = menu->exec(m_ui->propertyList->mapToGlobal(pos));
		if (action == remove_component_action)
		{
			uint32_t cmp_hash = 0;
			QByteArray label = m_ui->propertyList->itemAt(pos)->text(0).toLatin1();
			for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
			{
				if (strcmp(component_map[i], label.data()) == 0)
				{
					cmp_hash = crc32(component_map[i + 1]);
					break;
				}
			}
			const Lumix::WorldEditor::ComponentList& cmps = m_world_editor->getComponents(m_selected_entity);
			for (int i = 0, c = cmps.size(); i < c; ++i)
			{
				if (cmps[i].type == cmp_hash)
				{
					Lumix::Entity entity = cmps[i].entity;
					m_world_editor->destroyComponent(cmps[i]);
					Lumix::Array<Lumix::Entity> tmp(m_world_editor->getAllocator());
					tmp.push(m_selected_entity);
					onEntitySelected(tmp);
					break;
				}
			}
		}
	}
}


void PropertyView::on_nameEdit_editingFinished()
{
	if (m_selected_entity.isValid() && strcmp(m_ui->nameEdit->text().toLatin1().data(), m_selected_entity.getName()) != 0)
	{
		if (m_selected_entity.universe->nameExists(m_ui->nameEdit->text().toLatin1().data()))
		{
			static bool is = false;
			if (!is)
			{
				is = true;
				QMessageBox::critical(NULL, "Error", "Name already taken", QMessageBox::StandardButton::Ok, 0);
				is = false;
			}
		}
		else
		{
			m_world_editor->setEntityName(m_selected_entity, m_ui->nameEdit->text().toLatin1().data());
		}
	}
}

