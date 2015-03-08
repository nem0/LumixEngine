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
#include "mainwindow.h"
#include "property_view/file_edit.h"
#include "property_view/property_editor.h"
#include "property_view/terrain_editor.h"
#include "script/script_system.h"
#include "scripts/scriptcompiler.h"
#include <QColorDialog>
#include <qcombobox.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
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
	"Global light", "global_light",
	"Mesh", "renderable",
	"Physics Box", "box_rigid_actor",
	"Physics Controller", "physical_controller",
	"Physics Mesh", "mesh_rigid_actor",
	"Physics Heightfield", "physical_heightfield",
	"Point light", "point_light",
	"Script", "script",
	"Terrain", "terrain"
};


static const uint32_t TERRAIN_HASH = crc32("terrain");
static const uint32_t SCRIPT_HASH = crc32("script");


#pragma region new_props


void createComponentPropertyEditor(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::OutputBlob& stream, QTreeWidgetItem* property_item);


template <typename T>
T getPropertyValue(Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, int array_index, Lumix::OutputBlob& stream)
{
	T v;
	stream.clear();
	desc->get(cmp, array_index, stream);
	Lumix::InputBlob blob(stream.getData(), stream.getSize());
	blob.read(v);
	return v;
}


template <typename T>
PropertyEditor<T> createComponentPropertyEditor(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::OutputBlob& stream, QTreeWidgetItem* item)
{
	return PropertyEditor<T>::create(
		desc->getName(), 
		item, 
		getPropertyValue<T>(desc, cmp, array_index, stream), 
		[&view, desc, cmp, array_index](T v) { view.getWorldEditor()->setProperty(cmp.type, array_index, *desc, &v, sizeof(v)); }
	);
}


template <>
PropertyEditor<const char*> createComponentPropertyEditor<const char*>(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::OutputBlob& stream, QTreeWidgetItem* item)
{
	stream.clear();
	desc->get(cmp, array_index, stream);
	Lumix::InputBlob blob(stream.getData(), stream.getSize());
	return PropertyEditor<const char*>::create(desc->getName(), item, (const char*)blob.getData(), [&view, desc, cmp, array_index](const char* v) { view.getWorldEditor()->setProperty(cmp.type, array_index, *desc, v, strlen(v) + 1); });
}


template <>
class PropertyEditor<Lumix::Entity>
{
public:
	static PropertyEditor<Lumix::Entity> create(PropertyView& view, QTreeWidgetItem* item, Lumix::Entity e)
	{
		auto& cmps = view.getWorldEditor()->getComponents(e);
		addArray(view, "Entity", item,
			[&](int i)
			{
				return cmps[i];
			},

			[&cmps](int i)
			{
				Lumix::Component cmp = cmps[i];
				for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
				{
					if (crc32(component_map[i + 1]) == cmp.type)
					{
						return component_map[i];
					}
				}
				return (const char*)NULL;
			},

			[&cmps]()
			{
				return cmps.size();
			},

			[&view, e](QPushButton* button)
			{
				struct CB : public QComboBox
				{
					public:
						CB(QWidget* parent)
							: QComboBox(parent)
						{}

						virtual void hidePopup() override
						{
							deleteLater();
						}
				};

				CB* combobox = new CB(view.getMainWindow());
				for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
				{
					combobox->addItem(component_map[i]);
				}
				combobox->connect(combobox, (void (QComboBox::*)(int))&QComboBox::activated, [&view, e, combobox](int value) {
					for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
					{
						if (combobox->itemText(value) == component_map[i])
						{
							view.getWorldEditor()->addComponent(crc32(component_map[i + 1]));
							Lumix::Array<Lumix::Entity> tmp(view.getWorldEditor()->getAllocator());
							tmp.push(e);
							view.getWorldEditor()->selectEntities(&e, 1);
							return;
						}
					}
					combobox->deleteLater();
				});
				combobox->move(combobox->mapFromGlobal(button->mapToGlobal(button->pos())));
				combobox->raise();
				combobox->show();
				combobox->showPopup();
				combobox->setFocus();
			}
		);

		return PropertyEditor<Lumix::Entity>();
	};
};


void createComponentResourcePropertyEdit(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::OutputBlob& stream, QTreeWidgetItem* item)
{
	stream.clear();
	desc->get(cmp, array_index, stream);
	Lumix::InputBlob blob(stream.getData(), stream.getSize());
	auto* res = view.getResource((const char*)blob.getData());
	auto editor = PropertyEditor<Lumix::Resource*>::create(view, desc->getName(), item, res, [&view, desc, cmp, array_index](const char* v) { view.getWorldEditor()->setProperty(cmp.type, array_index, *desc, v, strlen(v) + 1); });
	auto file_desc = dynamic_cast<Lumix::IFilePropertyDescriptor*>(desc);
	auto filter = file_desc->getFileType();
	editor->setFilter(filter);
}


void createComponentFilePropertyEdit(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::OutputBlob& stream, QTreeWidgetItem* item)
{
	stream.clear();
	desc->get(cmp, array_index, stream);
	Lumix::InputBlob blob(stream.getData(), stream.getSize());
	auto editor = PropertyEditor<Lumix::Path>::create(view, desc->getName(), item, Lumix::Path((const char*)blob.getData()), [&view, desc, cmp, array_index](const char* v) { view.getWorldEditor()->setProperty(cmp.type, array_index, *desc, v, strlen(v) + 1); });
	auto file_desc = dynamic_cast<Lumix::IFilePropertyDescriptor*>(desc);
	auto filter = file_desc->getFileType();
	editor->setFilter(filter);
}


void createComponentArrayPropertyEdit(PropertyView& view, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::OutputBlob& stream, QTreeWidgetItem* property_item)
{
	QTreeWidgetItem* array_item = new QTreeWidgetItem();
	property_item->addChild(array_item);

	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* button = new QPushButton(" + ");
	layout->addWidget(button);
	layout->addStretch(1);
	array_item->setText(0, desc->getName());
	array_item->treeWidget()->setItemWidget(array_item, 1, widget);
	auto& array_desc = static_cast<Lumix::IArrayDescriptor&>(*desc);

	button->connect(button, &QPushButton::clicked, [array_item, cmp, &array_desc, &view]()
	{
		view.getWorldEditor()->addArrayPropertyItem(cmp, array_desc);

		QTreeWidgetItem* item = new QTreeWidgetItem();
		array_item->addChild(item);
		item->setText(0, QString::number(array_desc.getCount(cmp) - 1));
		auto& children = array_desc.getChildren();
		Lumix::OutputBlob stream(view.getWorldEditor()->getAllocator());
		for (int i = 0; i < children.size(); ++i)
		{
			createComponentPropertyEditor(view, array_desc.getCount(cmp) - 1, children[i], Lumix::Component(cmp), stream, item);
		}
		item->setExpanded(true);
	});

	auto& children = desc->getChildren();
	for (int j = 0; j < array_desc.getCount(cmp); ++j)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		array_item->addChild(item);
		item->setText(0, QString::number(j));
		QWidget* widget = new QWidget();
		QHBoxLayout* layout = new QHBoxLayout(widget);
		layout->setContentsMargins(0, 0, 0, 0);
		QPushButton* button = new QPushButton(" - ");
		button->connect(button, &QPushButton::clicked, [j, &view, cmp, &array_desc, item]()
		{
			item->parent()->removeChild(item);
			view.getWorldEditor()->removeArrayPropertyItem(cmp, j, array_desc);
		});
		layout->addStretch(1);
		layout->addWidget(button);
		item->treeWidget()->setItemWidget(item, 1, widget);
		for (int i = 0; i < children.size(); ++i)
		{
			createComponentPropertyEditor(view, j, children[i], cmp, stream, item);
		}
	}
}


void createComponentPropertyEditor(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::OutputBlob& stream, QTreeWidgetItem* property_item)
{
	switch (desc->getType())
	{
		case Lumix::IPropertyDescriptor::ARRAY:
			createComponentArrayPropertyEdit(view, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::BOOL:
			createComponentPropertyEditor<bool>(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::COLOR:
			createComponentPropertyEditor<Lumix::Vec4>(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::DECIMAL:
			{
				auto* decimal_desc = static_cast<Lumix::IDecimalPropertyDescriptor*>(desc);
				auto edit = createComponentPropertyEditor<float>(view, array_index, desc, cmp, stream, property_item);
				edit.setMinimum(decimal_desc->getMin());
				edit.setMaximum(decimal_desc->getMax());
				edit.setStep(decimal_desc->getStep());
			}
			break;
		case Lumix::IPropertyDescriptor::FILE:
			createComponentFilePropertyEdit(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::RESOURCE:
			createComponentResourcePropertyEdit(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::INTEGER:
			{
				auto* int_desc = static_cast<Lumix::IIntPropertyDescriptor*>(desc);
				auto edit = createComponentPropertyEditor<int>(view, array_index, desc, cmp, stream, property_item);
				edit.setMinimum(int_desc->getMin());
				edit.setMaximum(int_desc->getMax());
			}
			break;
		case Lumix::IPropertyDescriptor::STRING:
			createComponentPropertyEditor<const char*>(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::VEC3:
			createComponentPropertyEditor<Lumix::Vec3>(view, array_index, desc, cmp, stream, property_item);
			break;
		default:
			ASSERT(false);
			break;
	}
}


template <>
class PropertyEditor<Lumix::Component>
{
public:
	static PropertyEditor<Lumix::Component> create(PropertyView& view, const char* name, QTreeWidgetItem* item, Lumix::Component value)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		QWidget* widget = new QWidget();
		QHBoxLayout* layout = new QHBoxLayout(widget);
		layout->setContentsMargins(0, 0, 0, 0);
		QPushButton* button = new QPushButton(" - ");
		button->connect(button, &QPushButton::clicked, [subitem, &view, value]()
		{
			subitem->parent()->removeChild(subitem);
			view.getWorldEditor()->destroyComponent(value);
		});
		layout->addStretch(1);
		layout->addWidget(button);
		subitem->treeWidget()->setItemWidget(subitem, 1, widget);


		Lumix::OutputBlob stream(view.getWorldEditor()->getAllocator());
		auto& descriptors = view.getWorldEditor()->getPropertyDescriptors(value.type);
		for (int j = 0; j < descriptors.size(); ++j)
		{
			stream.clear();
			auto desc = descriptors[j];

			createComponentPropertyEditor(view, -1, desc, value, stream, subitem);
		}

		view.createCustomProperties(subitem, value);

		return PropertyEditor<Lumix::Component>();
	}
};


#pragma endregion


PropertyView::PropertyView(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::PropertyView)
	, m_selected_resource(NULL)
	, m_selected_entity(Lumix::Entity::INVALID)
{
	m_ui->setupUi(this);
}


PropertyView::~PropertyView()
{
	m_world_editor->entitySelected().unbind<PropertyView, &PropertyView::onEntitySelected>(this);
	m_world_editor->universeCreated().unbind<PropertyView, &PropertyView::onUniverseCreated>(this);
	m_world_editor->universeDestroyed().unbind<PropertyView, &PropertyView::onUniverseDestroyed>(this);
	onUniverseCreated();
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
	clear();
	onEntitySelected(getWorldEditor()->getSelectedEntities());
}


Lumix::WorldEditor* PropertyView::getWorldEditor()
{
	return m_world_editor;
}


void PropertyView::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_world_editor = &editor;
	
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


Lumix::Resource* PropertyView::getResource(const char* filename)
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
		return manager->load(Lumix::Path(rel_path));
	}
	else
	{
		return NULL;
	}
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


void PropertyView::onSelectedResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state)
{
	if (new_state == Lumix::Resource::State::READY)
	{
		clear();

		QTreeWidgetItem* item = new QTreeWidgetItem();
		m_ui->propertyList->insertTopLevelItem(0, item);
		if (dynamic_cast<Lumix::Model*>(m_selected_resource))
		{
			PropertyEditor<Lumix::Model*>::create(*this, item, (Lumix::Model*)m_selected_resource);
		}
		else if (dynamic_cast<Lumix::Material*>(m_selected_resource))
		{
			PropertyEditor<Lumix::Material*>::create(*this, "material", item, (Lumix::Material*)m_selected_resource);
		}
		else if (dynamic_cast<Lumix::Texture*>(m_selected_resource))
		{
			PropertyEditor<Lumix::Texture*>::create(*this, "texture", item, (Lumix::Texture*)m_selected_resource);
		}
		m_ui->propertyList->expandToDepth(1);
		m_ui->propertyList->resizeColumnToContents(0);
		m_ui->propertyList->expandToDepth(1);
		m_ui->propertyList->resizeColumnToContents(0);
	}
}


void PropertyView::clear()
{
	m_ui->propertyList->clear();
	for (int i = 0; i < m_entity_component_plugins.size(); ++i)
	{
		m_entity_component_plugins[i]->onPropertyViewCleared();
	}
}


void PropertyView::addEntityComponentPlugin(IEntityComponentPlugin* plugin)
{
	m_entity_component_plugins.push_back(plugin);
}


void PropertyView::createCustomProperties(QTreeWidgetItem* component_item, const Lumix::Component& component)
{
	for (int i = 0; i < m_entity_component_plugins.size(); ++i)
	{
		if (component.type == m_entity_component_plugins[i]->getType())
		{
			m_entity_component_plugins[i]->createEditor(component_item, component);
			return;
		}
	}
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
		clear();
		PropertyEditor<Lumix::Entity>::create(*this, NULL, e[0]);
		m_ui->propertyList->expandToDepth(1);
		m_ui->propertyList->resizeColumnToContents(0);		
		onEntityPosition(e[0]);
		m_ui->nameEdit->setText(e[0].getName());
	}
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


uint32_t TerrainComponentPlugin::getType()
{
	return crc32("terrain");
}


void TerrainComponentPlugin::onPropertyViewCleared()
{
	m_texture_tool_item = NULL;
	m_tools_item = NULL;
}


void TerrainComponentPlugin::createEditor(QTreeWidgetItem* component_item, const Lumix::Component& component)
{
	m_terrain_editor->m_tree_top_level = component_item;
	m_terrain_editor->m_component = component;
	QTreeWidgetItem* tools_item = new QTreeWidgetItem(QStringList() << "Tools");
	component_item->addChild(tools_item);
	m_tools_item = tools_item;

	{
		QWidget* widget = new QWidget();
		QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Save");
		tools_item->addChild(item);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		QPushButton* height_button = new QPushButton("Heightmap", widget);
		layout->addWidget(height_button);
		QPushButton* texture_button = new QPushButton("Splatmap", widget);
		layout->addWidget(texture_button);
		layout->setContentsMargins(2, 2, 2, 2);
		component_item->treeWidget()->setItemWidget(item, 1, widget);
		height_button->connect(height_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform("hm_texture")->save();
		});
		texture_button->connect(texture_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform("splat_texture")->save();
		});
	}

	QSlider* slider = new QSlider(Qt::Orientation::Horizontal);
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Brush size");
	tools_item->addChild(item);
	component_item->treeWidget()->setItemWidget(item, 1, slider);
	slider->setMinimum(1);
	slider->setMaximum(100);
	slider->connect(slider, &QSlider::valueChanged, [this](int value)
	{
		m_terrain_editor->m_terrain_brush_size = value;
	});

	slider = new QSlider(Qt::Orientation::Horizontal);
	item = new QTreeWidgetItem(QStringList() << "Brush strength");
	tools_item->addChild(item);
	component_item->treeWidget()->setItemWidget(item, 1, slider);
	slider->setMinimum(-100);
	slider->setMaximum(100);
	slider->connect(slider, &QSlider::valueChanged, [this](int value)
	{
		m_terrain_editor->m_terrain_brush_strength = value / 100.0f;
	});

	QWidget* widget = new QWidget();
	item = new QTreeWidgetItem(QStringList() << "Brush type");
	tools_item->addChild(item);
	QHBoxLayout* layout = new QHBoxLayout(widget);
	QPushButton* height_button = new QPushButton("Height", widget);
	layout->addWidget(height_button);
	QPushButton* texture_button = new QPushButton("Texture", widget);
	layout->addWidget(texture_button);
	QPushButton* entity_button = new QPushButton("Entity", widget);
	layout->addWidget(entity_button);
	layout->setContentsMargins(2, 2, 2, 2);
	component_item->treeWidget()->setItemWidget(item, 1, widget);
	m_terrain_editor->m_type = TerrainEditor::HEIGHT;
	height_button->connect(height_button, &QPushButton::clicked, [this]()
	{
		resetTools();
		m_terrain_editor->m_type = TerrainEditor::HEIGHT;
		if (m_terrain_editor->m_texture_tree_item)
		{
			m_terrain_editor->m_tree_top_level->removeChild(m_terrain_editor->m_texture_tree_item);
		}
	});
	texture_button->connect(texture_button, &QPushButton::clicked, this, &TerrainComponentPlugin::on_TerrainTextureTypeClicked);
	entity_button->connect(entity_button, &QPushButton::clicked, [this]()
	{
		resetTools();
		m_terrain_editor->m_type = TerrainEditor::ENTITY;
		if (m_terrain_editor->m_texture_tree_item)
		{
			m_terrain_editor->m_tree_top_level->removeChild(m_terrain_editor->m_texture_tree_item);
		}
	});
}


void TerrainComponentPlugin::resetTools()
{
	if (m_texture_tool_item)
	{
		m_texture_tool_item->parent()->removeChild(m_texture_tool_item);
		m_texture_tool_item = NULL;
	}
}


void TerrainComponentPlugin::on_TerrainTextureTypeClicked()
{
	resetTools();
	m_terrain_editor->m_type = TerrainEditor::TEXTURE;

	QComboBox* combobox = new QComboBox();
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Texture");
	m_texture_tool_item = item;
	m_tools_item->addChild(item);
	Lumix::Material* material = m_terrain_editor->getMaterial();
	if (material && material->isReady())
	{
		for (int i = 0; i < material->getTextureCount() - 2; ++i)
		{
			combobox->addItem(material->getTexture(i)->getPath().c_str());
		}
	}
	m_terrain_editor->m_tree_top_level->treeWidget()->setItemWidget(item, 1, combobox);

	m_terrain_editor->m_texture_tree_item = item;
	combobox->connect(combobox, (void (QComboBox::*)(int))&QComboBox::currentIndexChanged, [this](int value) { m_terrain_editor->m_texture_idx = value; });
}


uint32_t GlobalLightComponentPlugin::getType()
{
	return crc32("global_light");
}


void GlobalLightComponentPlugin::createEditor(QTreeWidgetItem* component_item, const Lumix::Component& component)
{
	QTreeWidgetItem* group_item = new QTreeWidgetItem(QStringList() << "Active");
	component_item->addChild(group_item);

	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);

	bool is_active = static_cast<Lumix::RenderScene*>(component.scene)->getActiveGlobalLight() == component;
	QLabel* label = new QLabel(is_active ? "Active" : "Inactive", widget);
	layout->addWidget(label);

	QPushButton* button = new QPushButton("Activate", widget);
	layout->addWidget(button);
	connect(button, &QPushButton::clicked, [component, label]() {
		static_cast<Lumix::RenderScene*>(component.scene)->setActiveGlobalLight(component);
		label->setText("Active");
	});

	group_item->treeWidget()->setItemWidget(group_item, 1, widget);
}


TerrainComponentPlugin::TerrainComponentPlugin(Lumix::WorldEditor& editor, EntityTemplateList* template_list, EntityList* entity_list)
{
	m_texture_tool_item = NULL;
	m_terrain_editor = new TerrainEditor(editor, template_list, entity_list);
}


TerrainComponentPlugin::~TerrainComponentPlugin()
{
	delete m_terrain_editor;
}


uint32_t ScriptComponentPlugin::getType()
{
	return crc32("script");
}


ScriptComponentPlugin::ScriptComponentPlugin(Lumix::WorldEditor& editor, ScriptCompiler& compiler)
	: m_world_editor(editor)
	, m_compiler(compiler)
	, m_status_item(NULL)
{
	connect(&m_compiler, &ScriptCompiler::compiled, [this](){
		setScriptStatus(m_compiler.getStatus());
	});
}


void ScriptComponentPlugin::onPropertyViewCleared()
{
	m_status_item = NULL;
}


void ScriptComponentPlugin::createEditor(QTreeWidgetItem* component_item, const Lumix::Component& component)
{
	QTreeWidgetItem* tools_item = new QTreeWidgetItem(QStringList() << "Tools");
	component_item->addChild(tools_item);
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* compile_button = new QPushButton("Compile", widget);
	layout->addWidget(compile_button);
	component_item->treeWidget()->setItemWidget(tools_item, 0, widget);
	compile_button->connect(compile_button, &QPushButton::clicked, this, [this, component](){
		Lumix::string path(m_world_editor.getAllocator());
		static_cast<Lumix::ScriptScene*>(component.scene)->getScriptPath(component, path);
		m_compiler.compileAll();
	});

	QTreeWidgetItem* status_item = new QTreeWidgetItem(QStringList() << "Status");
	m_status_item = status_item;
	component_item->addChild(status_item);
	Lumix::string path(m_world_editor.getAllocator());
	static_cast<Lumix::ScriptScene*>(component.scene)->getScriptPath(component, path);
	switch (m_compiler.getStatus())
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
	tools_item->setFirstColumnSpanned(true);
}


void ScriptComponentPlugin::setScriptStatus(uint32_t status)
{
	if (!m_status_item)
	{
		return;
	}
	switch (status)
	{
		case ScriptCompiler::SUCCESS:
			m_status_item->setText(1, "Success");
			break;
		case ScriptCompiler::NOT_COMPILED:
			m_status_item->setText(1, "Not compiled");
			break;
		case ScriptCompiler::UNKNOWN:
			m_status_item->setText(1, "Unknown");
			break;
		case ScriptCompiler::FAILURE:
			m_status_item->setText(1, "Failure");
			break;
		default:
			ASSERT(false);
			break;
	}	
}

