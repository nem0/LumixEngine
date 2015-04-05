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
#include "dynamic_object_model.h"
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
#include "property_view/terrain_editor.h"
#include "script/script_system.h"
#include "scripts/scriptcompiler.h"
#include <QColorDialog>
#include <qcombobox.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <qfileinfo.h>
#include <qmenu.h>
#include <qmessagebox.h>
#include <qmimedata.h>
#include <qlineedit.h>
#include <qpainter.h>
#include <qpushbutton.h>
#include <qstyleditemdelegate.h>
#include <qtextstream.h>
#include <qtreewidget.h>


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


static const char* getComponentName(Lumix::Component cmp)
{
	for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		if (cmp.type == crc32(component_map[i + 1]))
		{
			return component_map[i];
		}
	}
	return "Unknown component";
}


static const uint32_t TERRAIN_HASH = crc32("terrain");
static const uint32_t SCRIPT_HASH = crc32("script");


#pragma region new_props


Q_DECLARE_METATYPE(Lumix::Vec3);


class CustomItemDelegate : public QStyledItemDelegate
{
	public:
		CustomItemDelegate(QWidget* parent) : QStyledItemDelegate(parent) {}


		void setEditorData(QWidget* editor, const QModelIndex& index) const override
		{
			if (index.column() == 1 && index.data().type() == QMetaType::Float)
			{
				qobject_cast<QDoubleSpinBox*>(editor)->setValue(index.data().toFloat());
				return;
			}
			QStyledItemDelegate::setEditorData(editor, index);
		}


		bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem&, const QModelIndex& index) override
		{
			if (event->type() == QEvent::MouseButtonRelease)
			{
				auto* node = (DynamicObjectModel::Node*)index.internalPointer();
				if (!node)
				{
					return false;
				}
				if (node->m_adder)
				{
					QWidget* widget = qobject_cast<QWidget*>(parent());
					QPoint pos = widget->mapToGlobal(QPoint(static_cast<QMouseEvent*>(event)->x(), static_cast<QMouseEvent*>(event)->y()));
					node->m_adder(widget, pos);
					return true;
				}
				if (node->m_remover)
				{
					node->m_remover();
					return true;
				}
				if (index.data().type() == QMetaType::QColor)
				{
					QColorDialog* dialog = new QColorDialog(index.data().value<QColor>());
					dialog->setModal(true);
					auto old_color = index.data().value<QColor>();
					dialog->connect(dialog, &QColorDialog::rejected, [model, index, old_color]{
						model->setData(index, old_color);
					});
					dialog->connect(dialog, &QColorDialog::currentColorChanged, [model, index, dialog]()
					{
						QColor color = dialog->currentColor();
						Lumix::Vec4 value;
						value.x = color.redF();
						value.y = color.greenF();
						value.z = color.blueF();
						value.w = color.alphaF();
						model->setData(index, color);
					});
					dialog->show();
				}
				else if (index.data().type() == QMetaType::Bool)
				{
					model->setData(index, !index.data().toBool());
					return true;
				}
			}
			return false;
		}


		void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
		{
			if (index.column() == 1)
			{
				auto* node = (DynamicObjectModel::Node*)index.internalPointer();
				if (!node)
				{
					return;
				}
				if (node->m_adder)
				{
					painter->save();
					QStyleOptionButton button_style_option;
					button_style_option.rect = option.rect;
					button_style_option.text = "+";
					QApplication::style()->drawControl(QStyle::CE_PushButton, &button_style_option, painter);
					painter->restore();
					return;
				}
				if (node->m_remover)
				{
					painter->save();
					QStyleOptionButton button_style_option;
					button_style_option.rect = option.rect;
					button_style_option.text = "-";
					QApplication::style()->drawControl(QStyle::CE_PushButton, &button_style_option, painter);
					painter->restore();
					return;
				}
				QVariant data = index.data();
				if (data.type() == QMetaType::Bool)
				{
					painter->save();
					bool checked = data.toBool();
					QStyleOptionButton check_box_style_option;
					check_box_style_option.state |= QStyle::State_Enabled;
					check_box_style_option.state |= checked ? QStyle::State_On : QStyle::State_Off;
					check_box_style_option.rect = option.rect;
					QApplication::style()->drawControl(QStyle::CE_CheckBox, &check_box_style_option, painter);
					painter->restore();
					return;
				}
			}
			QStyledItemDelegate::paint(painter, option, index);
		}


		QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
		{
			if (index.column() == 1)
			{
				auto node = (DynamicObjectModel::Node*)index.internalPointer();
				if (!node)
				{
					return QStyledItemDelegate::createEditor(parent, option, index);
				}
				if (node->m_getter().type() == QMetaType::Bool)
				{
					return NULL;
				}
				else if (node->m_getter().type() == QMetaType::Float)
				{
					QDoubleSpinBox* input = new QDoubleSpinBox(parent);
					return input;
				}
			}
			return QStyledItemDelegate::createEditor(parent, option, index);
		}

};


class EntityModel : public DynamicObjectModel
{
	public:
		EntityModel(Lumix::WorldEditor& editor, Lumix::Entity entity)
			: m_editor(editor)
		{
			m_entity = entity;
			getRoot().m_name = "Entity";
			getRoot().m_adder = [this](QWidget* widget, QPoint pos) { addComponent(widget, pos); };
			
			addNameProperty();
			addPositionProperty();

			Lumix::WorldEditor::ComponentList& cmps = m_editor.getComponents(m_entity);
			for (int i = 0; i < cmps.size(); ++i)
			{
				Lumix::Component cmp = cmps[i];
				addComponentNode(cmp);
			}

			m_editor.propertySet().bind<EntityModel, &EntityModel::onPropertySet>(this);
			m_editor.componentAdded().bind<EntityModel, &EntityModel::onComponentAdded>(this);
			m_editor.componentDestroyed().bind<EntityModel, &EntityModel::onComponentDestroyed>(this);
		}


		~EntityModel()
		{
			m_editor.componentAdded().unbind<EntityModel, &EntityModel::onComponentAdded>(this);
			m_editor.componentDestroyed().unbind<EntityModel, &EntityModel::onComponentDestroyed>(this);
			m_editor.propertySet().unbind<EntityModel, &EntityModel::onPropertySet>(this);
			m_editor.getUniverse()->entityMoved().unbind<EntityModel, &EntityModel::onEntityPosition>(this);
		}


		void onComponentAdded(Lumix::Component component)
		{
			int row = m_editor.getComponents(component.entity).size() + 2;
			QModelIndex parent_index = createIndex(0, 0, &getRoot());
			beginInsertRows(parent_index, row, row);
			addComponentNode(component);
			endInsertRows();
		}


		void onComponentDestroyed(Lumix::Component component)
		{
			auto& cmps = m_editor.getComponents(component.entity);
			int row = cmps.indexOf(component) + 2;
			QModelIndex parent_index = createIndex(0, 0, &getRoot());
			beginRemoveRows(parent_index, row, row);
			delete getRoot().m_children[row];
			getRoot().m_children.removeAt(row);
			endRemoveRows();
		}


		void onPropertySet(Lumix::Component component, const Lumix::IPropertyDescriptor& descriptor)
		{
			if (component.entity == m_entity)
			{
				auto& cmps = m_editor.getComponents(component.entity);
				for (int i = 0; i < cmps.size(); ++i)
				{
					if (cmps[i] == component)
					{
						auto& descriptors = m_editor.getPropertyDescriptors(component.type);
						auto* node = getRoot().m_children[i + 2];
						for (int j = 0; j < node->m_children.size(); ++j)
						{
							if (descriptors[j] == &descriptor)
							{
								QModelIndex index = createIndex(j, 1, node->m_children[j]);
								emit dataChanged(index, index);
								break;
							}
						}
					}
				}
			}
		}


		void addNameProperty()
		{
			Node& name_node = getRoot().addChild("name");
			name_node.m_getter = [this]() -> QVariant { return m_entity.getName(); };
			name_node.m_setter = [this](const QVariant& value) {
				if (m_editor.getUniverse()->nameExists(value.toString().toLatin1().data()))
				{
					QMessageBox::warning(NULL, "Warning", "Entity with this name already exists!", QMessageBox::Ok);
				}
				else
				{
					m_editor.setEntityName(m_entity, value.toString().toLatin1().data());
				}
			};
		}


		void setEntityPosition(int index, float value)
		{
			Lumix::Vec3 v = m_entity.getPosition(); 
			((float*)&v)[index] = value; 
			Lumix::StackAllocator<256> allocator;
			Lumix::Array<Lumix::Entity> entities(allocator);
			Lumix::Array<Lumix::Vec3> positions(allocator);
			entities.push(m_entity);
			positions.push(v);
			m_editor.setEntitiesPositions(entities, positions);
		}


		void addPositionProperty()
		{
			Node& position_node = getRoot().addChild("position");
			position_node.m_getter = [this]() -> QVariant { 
				Lumix::Vec3 pos = m_entity.getPosition();
				return QString("%1; %2; %3").arg(pos.x, 0, 'f', 6).arg(pos.y, 0, 'f', 6).arg(pos.z, 0, 'f', 6);
			};

			Node& x_node = position_node.addChild("x");
			x_node.m_getter = [this]() -> QVariant { return m_entity.getPosition().x; };
			x_node.m_setter = [this](const QVariant& value) { setEntityPosition(0, value.toFloat()); };
			Node& y_node = position_node.addChild("y");
			y_node.m_getter = [this]() -> QVariant { return m_entity.getPosition().y; };
			y_node.m_setter = [this](const QVariant& value) { setEntityPosition(1, value.toFloat()); };
			Node& z_node = position_node.addChild("z");
			z_node.m_getter = [this]() -> QVariant { return m_entity.getPosition().z; };
			z_node.m_setter = [this](const QVariant& value) { setEntityPosition(2, value.toFloat()); };

			m_editor.getUniverse()->entityMoved().bind<EntityModel, &EntityModel::onEntityPosition>(this);
		}

		void onEntityPosition(const Lumix::Entity& entity)
		{
			if (entity == m_entity)
			{
				QModelIndex index = createIndex(1, 1, getRoot().m_children[1]);
				QModelIndex index_x = createIndex(0, 1, getRoot().m_children[1]->m_children[0]);
				QModelIndex index_z = createIndex(0, 2, getRoot().m_children[1]->m_children[2]);
				emit dataChanged(index, index);
				emit dataChanged(index_x, index_z);
			}
		}


		void addComponentNode(Lumix::Component cmp)
		{
			Node& node = getRoot().addChild(getComponentName(cmp));
			node.m_getter = []() -> QVariant { return ""; };
			node.m_remover = [this, cmp]() { m_editor.destroyComponent(cmp); };
			auto& descriptors = m_editor.getPropertyDescriptors(cmp.type);
			for (int j = 0; j < descriptors.size(); ++j)
			{
				auto* desc = descriptors[j];
				Node& child = node.addChild(desc->getName());
				child.m_getter = [desc, cmp, this]() -> QVariant
				{
					return this->get(cmp, -1, desc);
				};
				switch (desc->getType())
				{
					case Lumix::IPropertyDescriptor::ARRAY:
					{
						auto* array_desc = static_cast<Lumix::IArrayDescriptor*>(desc);
						for (int k = 0; k < array_desc->getCount(cmp); ++k)
						{
							Node& array_item_node = child.addChild(QString("%1").arg(k));
							array_item_node.m_getter = []() -> QVariant { return ""; };
							for (int l = 0; l < array_desc->getChildren().size(); ++l)
							{
								auto* array_item_property_desc = array_desc->getChildren()[l];
								Node& subchild = array_item_node.addChild(array_item_property_desc->getName());
								subchild.m_getter = [k, array_item_property_desc, cmp, this]() -> QVariant
								{
									return this->get(cmp, k, array_item_property_desc);
								};
								subchild.m_setter = [k, array_item_property_desc, cmp, this](const QVariant& value)
								{
									this->set(cmp, k, array_item_property_desc, value);
								};
							}
						}
						break;
					}
					case Lumix::IPropertyDescriptor::VEC3:
					{
						Node& x_node = child.addChild("x");
						x_node.m_getter = [desc, cmp]() -> QVariant { return desc->getValue<Lumix::Vec3>(cmp).x; };
						x_node.m_setter = [desc, cmp](const QVariant& value) { Lumix::Vec3 v = desc->getValue<Lumix::Vec3>(cmp); v.x = value.toFloat(); desc->setValue(cmp, v); };
						Node& y_node = child.addChild("y");
						y_node.m_getter = [desc, cmp]() -> QVariant { return desc->getValue<Lumix::Vec3>(cmp).y; };
						y_node.m_setter = [desc, cmp](const QVariant& value) { Lumix::Vec3 v = desc->getValue<Lumix::Vec3>(cmp); v.y = value.toFloat(); desc->setValue(cmp, v); };
						Node& z_node = child.addChild("z");
						z_node.m_getter = [desc, cmp]() -> QVariant { return desc->getValue<Lumix::Vec3>(cmp).z; };
						z_node.m_setter = [desc, cmp](const QVariant& value) { Lumix::Vec3 v = desc->getValue<Lumix::Vec3>(cmp); v.z = value.toFloat(); desc->setValue(cmp, v); };
						break;
					}
				default:
					child.m_setter = [desc, cmp, this](const QVariant& value)
					{
						this->set(cmp, -1, desc, value);
					};
					break;
				}
			}
		}

		void addComponent(QWidget* widget, QPoint pos) 
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

			CB* combobox = new CB(widget);
			for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
			{
				combobox->addItem(component_map[i]);
			}
			combobox->connect(combobox, (void (QComboBox::*)(int))&QComboBox::activated, [this, combobox](int value) {
				for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
				{
					if (combobox->itemText(value) == component_map[i])
					{
						if (!m_editor.getComponent(m_entity, crc32(component_map[i + 1])).isValid())
						{
							m_editor.addComponent(crc32(component_map[i + 1]));
						}
						break;
					}
				}
				combobox->deleteLater();
				
			});
			combobox->move(combobox->mapFromGlobal(pos));
			combobox->raise();
			combobox->showPopup();
			combobox->setFocus();
		};

		void set(Lumix::Component cmp, int index, Lumix::IPropertyDescriptor* desc, QVariant value)
		{
			switch (desc->getType())
			{
				case Lumix::IPropertyDescriptor::BOOL:
					{
						bool b = value.toBool();
						m_editor.setProperty(cmp.type, index, *desc, &b, sizeof(b));
						break;
					}
				case Lumix::IPropertyDescriptor::COLOR:
					{
						QColor color = value.value<QColor>();
						Lumix::Vec4 v;
						v.x = color.redF();
						v.y = color.greenF();
						v.z = color.blueF();
						v.w = color.alphaF();
						m_editor.setProperty(cmp.type, index, *desc, &v, sizeof(v));
						break;
				}
				case Lumix::IPropertyDescriptor::DECIMAL:
					{
						float f = value.toFloat();
						m_editor.setProperty(cmp.type, index, *desc, &f, sizeof(f));
						break;
				}
				case Lumix::IPropertyDescriptor::INTEGER:
					{
						int i = value.toInt();
						m_editor.setProperty(cmp.type, index, *desc, &i, sizeof(i));
						break;
					}
				case Lumix::IPropertyDescriptor::RESOURCE:
				case Lumix::IPropertyDescriptor::FILE:
				case Lumix::IPropertyDescriptor::STRING:
					{
						auto tmp = value.toString().toLatin1();
						m_editor.setProperty(cmp.type, index, *desc, tmp.data(), tmp.length());
					}
					break;
				default:
					Q_ASSERT(false);
					break;
			}
		}

		QVariant get(Lumix::Component cmp, int index, Lumix::IPropertyDescriptor* desc)
		{
			Lumix::OutputBlob stream(m_editor.getAllocator());
			if (index == -1)
			{
				desc->get(cmp, stream);
			}
			else
			{
				desc->get(cmp, index, stream);
			}
			Lumix::InputBlob input(stream);
			switch (desc->getType())
			{
				case Lumix::IPropertyDescriptor::BOOL:
					{
						bool b;
						input.read(b);
						return b;
					}
				case Lumix::IPropertyDescriptor::DECIMAL:
					{
						float f;
						input.read(f);
						return f;
					}
				case Lumix::IPropertyDescriptor::INTEGER:
					{
						int i;
						input.read(i);
						return i;
					}
				case Lumix::IPropertyDescriptor::COLOR:
					{
						Lumix::Vec4 c;
						input.read(c);
						QColor color((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255));
						return color;
					}
				case Lumix::IPropertyDescriptor::VEC3:
					{
						Lumix::Vec3 v;
						input.read(v);
						return QString("%1; %2; %3").arg(v.x).arg(v.y).arg(v.z);
					}
				case Lumix::IPropertyDescriptor::STRING:
				case Lumix::IPropertyDescriptor::RESOURCE:
				case Lumix::IPropertyDescriptor::FILE:
					{
						return (const char*)stream.getData();
					}
				case Lumix::IPropertyDescriptor::ARRAY:
					return QString("%1 members").arg(static_cast<Lumix::IArrayDescriptor*>(desc)->getCount(cmp));
					break;
				default:
					Q_ASSERT(false);
					break;
			}

			return QVariant();
		}

		Lumix::Entity getEntity() const
		{
			return m_entity;
		}

	private:
		Lumix::WorldEditor& m_editor;
		Lumix::Entity m_entity;
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
	delete m_ui;
}


void PropertyView::setModel(QAbstractItemModel* model)
{
	delete m_ui->treeView->model();
	m_ui->treeView->setModel(model);
}


Lumix::WorldEditor* PropertyView::getWorldEditor()
{
	return m_world_editor;
}


void PropertyView::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_world_editor = &editor;
	m_world_editor->universeDestroyed().bind<PropertyView, &PropertyView::onUniverseDestroyed>(this);
	m_world_editor->getUniverse()->entityDestroyed().bind<PropertyView, &PropertyView::onEntityDestroyed>(this);
	m_world_editor->entitySelected().bind<PropertyView, &PropertyView::onEntitySelected>(this);
}


void PropertyView::onEntityDestroyed(const Lumix::Entity& entity)
{
	if (m_selected_entity == entity)
	{
		setModel(NULL);
	}
}


void PropertyView::onUniverseDestroyed()
{
	setModel(NULL);
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


void PropertyView::onSelectedResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state)
{
	if (new_state == Lumix::Resource::State::READY)
	{
		if (dynamic_cast<Lumix::Model*>(m_selected_resource))
		{
			using Lumix::Model;
			Model* model = (Model*)m_selected_resource;
			DynamicObjectModel* item_model = new DynamicObjectModel;
			auto object = item_model->object("Model", model);
			object
				.property("Bone count", &Model::getBoneCount)
				.property("Bounding radius", &Model::getBoundingRadius)
				.array("Meshes", model->getMeshCount(), &Model::getMeshPtr, [](const Lumix::Mesh* mesh) -> const char* { return mesh->getName(); })
					.property("Triangles", &Lumix::Mesh::getTriangleCount)
					.property("Material", [](const Lumix::Mesh* mesh) -> const char* { return mesh->getMaterial()->getPath().c_str(); });
			m_ui->treeView->setItemDelegateForColumn(1, new CustomItemDelegate(m_ui->treeView));
			setModel(item_model);
			m_ui->treeView->expandAll();
		}
		else if (dynamic_cast<Lumix::Material*>(m_selected_resource))
		{
			using Lumix::Material;
			Material* material = (Material*)m_selected_resource;
			DynamicObjectModel* model = new DynamicObjectModel;
			auto object = model->object("Material", material);
			object
				.property("Alpha cutout", &Material::isAlphaCutout, &Material::enableAlphaCutout)
				.property("Alpha to coverage", &Material::isAlphaToCoverage, &Material::enableAlphaToCoverage)
				.property("Backface culling", &Material::isBackfaceCulling, &Material::enableBackfaceCulling)
				.property("Shadow receiver", &Material::isShadowReceiver, &Material::enableShadowReceiving)
				.property("Z test", &Material::isZTest, &Material::enableZTest)
				.property("Shader", [](Material* material) -> const char* { return material->getShader()->getPath().c_str(); });
			object
				.array("Textures", material->getTextureCount(), &Material::getTexture, [](Lumix::Texture* texture) -> const char* { return texture->getPath().c_str(); })
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
						, [name](Material* material) -> QVariant {
							Material::Uniform* uniform = getMaterialUniform(material, name);
							if (uniform)
							{
								switch (uniform->m_type)
								{
									case Material::Uniform::FLOAT:
										return uniform->m_float;
								}
							}
							return QVariant(); 
						}
						, [name](Material* material, const QVariant& value) {
							Material::Uniform* uniform = getMaterialUniform(material, name);
							if (uniform)
							{
								switch (uniform->m_type)
								{
									case Material::Uniform::FLOAT:
										uniform->m_float = value.toFloat();
										break;
								}
							}
						}
					);
				}
			}
				
			m_ui->treeView->setItemDelegateForColumn(1, new CustomItemDelegate(m_ui->treeView));
			setModel(model);
			m_ui->treeView->expandAll();
		}
		else if (dynamic_cast<Lumix::Texture*>(m_selected_resource))
		{
			using Lumix::Texture;
			Texture* texture = (Texture*)m_selected_resource;
			DynamicObjectModel* model = new DynamicObjectModel;
			auto object = model->object("Texture", texture);
			object
				.property("Width", &Texture::getWidth)
				.property("Height", &Texture::getHeight)
				.property("Bytes per pixel", &Texture::getBytesPerPixel);

			m_ui->treeView->setItemDelegateForColumn(1, new CustomItemDelegate(m_ui->treeView));
			setModel(model);
			m_ui->treeView->expandAll();
			m_ui->treeView->resizeColumnToContents(0);
		}
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
	if (e.size() == 1 && e[0].isValid())
	{
		EntityModel* model = new EntityModel(*m_world_editor, m_selected_entity);
		m_ui->treeView->setItemDelegateForColumn(1, new CustomItemDelegate(m_ui->treeView));
		setModel(model);
		m_ui->treeView->expandAll();
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
	connect(&m_compiler, &ScriptCompiler::compiled, [this](const QString& module_name){
		QFileInfo info(m_world_editor.getUniversePath().c_str());
		if (module_name == info.baseName())
		{
			setScriptStatus(m_compiler.getStatus(module_name));
		}
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
		m_compiler.onScriptChanged(path.c_str());
	});

	QTreeWidgetItem* status_item = new QTreeWidgetItem(QStringList() << "Status");
	m_status_item = status_item;
	component_item->addChild(status_item);
	Lumix::string path(m_world_editor.getAllocator());
	static_cast<Lumix::ScriptScene*>(component.scene)->getScriptPath(component, path);
	QFileInfo info(m_world_editor.getUniversePath().c_str());
	switch (m_compiler.getStatus("universe"))
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
