#include "entity_model.h"
#include "core/math_utils.h"
#include "core/path.h"
#include "editor/world_editor.h"
#include "property_view.h"
#include <qapplication.h>
#include <qboxlayout.h>
#include <qcombobox.h>
#include <qfiledialog.h>
#include <qlineedit.h>
#include <qmessagebox.h>
#include <qmimedata.h>
#include <qpushbutton.h>


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
	for (int i = 0; i < lengthOf(component_map); i += 2)
	{
		if (cmp.type == crc32(component_map[i + 1]))
		{
			return component_map[i];
		}
	}
	return "Unknown component";
}


EntityModel::EntityModel(PropertyView& view, Lumix::WorldEditor& editor, Lumix::Entity entity)
	: m_editor(editor)
	, m_view(view)
	, m_is_setting(false)
{
	m_entity = entity;
	getRoot().m_name = "Entity";
	getRoot().onCreateEditor = [this](QWidget* parent, const QStyleOptionViewItem&){
		auto button = new QPushButton(" + ", parent);
		connect(button, &QPushButton::clicked, [this, button](){ addComponent(button, button->mapToGlobal(button->pos())); });
		return button;
	};
	getRoot().enablePeristentEditor();
	addNameProperty();
	addPositionProperty();

	Lumix::WorldEditor::ComponentList& cmps = m_editor.getComponents(m_entity);
	for (int i = 0; i < cmps.size(); ++i)
	{
		Lumix::Component cmp = cmps[i];
		addComponentNode(cmp, i);
	}

	m_editor.getUniverse()->entityDestroyed().bind<EntityModel, &EntityModel::onEntityDestroyed>(this);
	m_editor.universeDestroyed().bind<EntityModel, &EntityModel::onUniverseDestroyed>(this);
	m_editor.propertySet().bind<EntityModel, &EntityModel::onPropertySet>(this);
	m_editor.componentAdded().bind<EntityModel, &EntityModel::onComponentAdded>(this);
	m_editor.componentDestroyed().bind<EntityModel, &EntityModel::onComponentDestroyed>(this);
}


void EntityModel::reset(const QString& reason)
{
	beginResetModel();
	m_entity = Lumix::Entity::INVALID;
	for (int i = 0; i < getRoot().m_children.size(); ++i)
	{
		delete getRoot().m_children[i];
		getRoot().m_children.clear();
		getRoot().m_getter = [reason]() -> QVariant { return reason; };
	}
	endResetModel();
}


void EntityModel::onEntityDestroyed(const Lumix::Entity& entity)
{
	if (entity == m_entity)
	{
		reset("Entity destroyed");
	}
}


void EntityModel::onUniverseDestroyed()
{
	reset("Universe destroyed");
}


EntityModel::~EntityModel()
{
	m_editor.getUniverse()->entityDestroyed().unbind<EntityModel, &EntityModel::onEntityDestroyed>(this);
	m_editor.universeDestroyed().unbind<EntityModel, &EntityModel::onUniverseDestroyed>(this);
	m_editor.componentAdded().unbind<EntityModel, &EntityModel::onComponentAdded>(this);
	m_editor.componentDestroyed().unbind<EntityModel, &EntityModel::onComponentDestroyed>(this);
	m_editor.propertySet().unbind<EntityModel, &EntityModel::onPropertySet>(this);
	m_editor.getUniverse()->entityMoved().unbind<EntityModel, &EntityModel::onEntityPosition>(this);
}


void EntityModel::onComponentAdded(Lumix::Component component)
{
	if (m_entity == component.entity)
	{
		auto& cmps = m_editor.getComponents(component.entity);
		int row = cmps.indexOf(component) + 3;
		QModelIndex parent_index = createIndex(0, 0, &getRoot());
		beginInsertRows(parent_index, row, row);
		addComponentNode(component, row - 3);
		endInsertRows();
	}
}


void EntityModel::onComponentDestroyed(Lumix::Component component)
{
	if (component.entity == m_entity)
	{
		auto& cmps = m_editor.getComponents(component.entity);
		int row = cmps.indexOf(component) + 3;
		QModelIndex parent_index = createIndex(0, 0, &getRoot());
		beginRemoveRows(parent_index, row, row);
		delete getRoot().m_children[row];
		getRoot().m_children.removeAt(row);
		endRemoveRows();
	}
}


void EntityModel::onPropertySet(Lumix::Component component, const Lumix::IPropertyDescriptor& descriptor)
{
	if (component.entity == m_entity && !m_is_setting)
	{
		auto& cmps = m_editor.getComponents(component.entity);
		for (int i = 0; i < cmps.size(); ++i)
		{
			if (cmps[i].type == component.type)
			{
				auto& descriptors = m_editor.getPropertyDescriptors(component.type);
				auto* node = getRoot().m_children[i + 3];
				for (int j = 0; j < node->m_children.size(); ++j)
				{
					if (descriptors[j] == &descriptor)
					{
						QModelIndex index = createIndex(j, 1, node->m_children[j]);
						emit dataChanged(index, index);
						return;
					}
				}
			}
		}
	}
}


void EntityModel::addNameProperty()
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


void EntityModel::setEntityRotation(int index, float value)
{
	auto axis_angle = m_entity.getRotation().getAxisAngle();
	((float*)&axis_angle)[index] = value;
	axis_angle.axis.normalize();
	Lumix::StackAllocator<256> allocator;
	Lumix::Array<Lumix::Entity> entities(allocator);
	Lumix::Array<Lumix::Quat> rotations(allocator);
	entities.push(m_entity);
	rotations.push(Lumix::Quat(axis_angle.axis, axis_angle.angle));
	m_editor.setEntitiesRotations(entities, rotations);
}


void EntityModel::setEntityPosition(int index, float value)
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


void EntityModel::addPositionProperty()
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

	Node& rotation_node = getRoot().addChild("rotation");
	rotation_node.m_getter = [this]() -> QVariant {
		auto rot = m_entity.getRotation().getAxisAngle();
		return QString("[%1; %2; %3] %4")
			.arg(rot.axis.x, 0, 'f', 6)
			.arg(rot.axis.y, 0, 'f', 6)
			.arg(rot.axis.z, 0, 'f', 6)
			.arg(Lumix::Math::radiansToDegrees(rot.angle), 0, 'f', 6);
	};

	{
		Node& x_node = rotation_node.addChild("x");
		x_node.m_getter = [this]() -> QVariant { return m_entity.getRotation().getAxisAngle().axis.x; };
		x_node.m_setter = [this](const QVariant& value) { setEntityRotation(0, value.toFloat()); };
		Node& y_node = rotation_node.addChild("y");
		y_node.m_getter = [this]() -> QVariant { return m_entity.getRotation().getAxisAngle().axis.y; };
		y_node.m_setter = [this](const QVariant& value) { setEntityRotation(1, value.toFloat());  };
		Node& z_node = rotation_node.addChild("z");
		z_node.m_getter = [this]() -> QVariant { return m_entity.getRotation().getAxisAngle().axis.z; };
		z_node.m_setter = [this](const QVariant& value) { setEntityRotation(2, value.toFloat()); };
		Node& angle_node = rotation_node.addChild("angle");
		angle_node.m_getter = [this]() -> QVariant { return Lumix::Math::radiansToDegrees(m_entity.getRotation().getAxisAngle().angle); };
		angle_node.m_setter = [this](const QVariant& value) { setEntityRotation(3, Lumix::Math::degreesToRadians(value.toFloat())); };
		DynamicObjectModel::setSliderEditor(angle_node, 0, 360, 5);
	}

	m_editor.getUniverse()->entityMoved().bind<EntityModel, &EntityModel::onEntityPosition>(this);
}

void EntityModel::onEntityPosition(const Lumix::Entity& entity)
{
	if (entity == m_entity)
	{
		QModelIndex index = createIndex(1, 1, getRoot().m_children[1]);
		QModelIndex index_x = createIndex(0, 1, getRoot().m_children[1]->m_children[0]);
		QModelIndex index_z = createIndex(2, 1, getRoot().m_children[1]->m_children[2]);
		emit dataChanged(index, index);
		emit dataChanged(index_x, index_z);
	}
}


void EntityModel::addResourceProperty(Node& child, Lumix::IPropertyDescriptor* desc, Lumix::Component cmp)
{
	child.m_setter = [desc, cmp, this](const QVariant& value)
	{
		this->set(cmp.entity, cmp.type, -1, desc, value);
	};
	child.onSetModelData = [desc, cmp, this](QWidget* editor){
		const auto& children = editor->children();
		auto value = qobject_cast<QLineEdit*>(children[1])->text();
		this->set(cmp.entity, cmp.type, -1, desc, value);
	};
	child.onDrop = [desc, cmp, this](const QMimeData* data, Qt::DropAction){
		QList<QUrl> urls = data->urls();
		Q_ASSERT(urls.size() < 2);
		if (urls.size() == 1)
		{
			char rel_path[LUMIX_MAX_PATH];
			Lumix::Path path(urls[0].toLocalFile().toLatin1().data());
			m_editor.getRelativePath(rel_path, LUMIX_MAX_PATH, path);
			this->set(cmp.entity, cmp.type, -1, desc, rel_path);
			return true;
		}
		return false;
	};
	child.onCreateEditor = [desc, cmp, this](QWidget* parent, const QStyleOptionViewItem&) -> QWidget* {
		QWidget* widget = new QWidget(parent);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		QLineEdit* edit = new QLineEdit(widget);
		layout->addWidget(edit); 
		QPushButton* button = new QPushButton("Browse", widget);
		connect(button, &QPushButton::clicked, [edit, parent, this](){
			auto value = QFileDialog::getOpenFileName();
			if (!value.isEmpty())
			{
				char rel_path[LUMIX_MAX_PATH];
				Lumix::Path path(value.toLatin1().data());
				m_editor.getRelativePath(rel_path, LUMIX_MAX_PATH, path);
				edit->setText(rel_path);
			}
		});
		layout->addWidget(button);
		QPushButton* go_button = new QPushButton("->", widget);
		connect(go_button, &QPushButton::clicked, [edit, this](){
			m_view.setSelectedResourceFilename(edit->text().toLatin1().data());
		});
		layout->addWidget(go_button);
		layout->setContentsMargins(0, 0, 0, 0);
		edit->setText(this->get(cmp.entity, cmp.type, -1, desc).toString());
		return widget;
	};

}


void EntityModel::addArrayProperty(Node& child, Lumix::IArrayDescriptor* array_desc, Lumix::Component cmp)
{
	child.onCreateEditor = [cmp, array_desc](QWidget* parent, const QStyleOptionViewItem&) {
		auto button = new QPushButton(" + ", parent);
		button->connect(button, &QPushButton::clicked, [cmp, array_desc](){ array_desc->addArrayItem(cmp, -1); });
		return button;
	};
	child.m_setter = [](const QVariant&){};
	child.m_is_persistent_editor = true;
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
				return this->get(cmp.entity, cmp.type, k, array_item_property_desc);
			};
			subchild.m_setter = [k, array_item_property_desc, cmp, this](const QVariant& value)
			{
				this->set(cmp.entity, cmp.type, k, array_item_property_desc, value);
			};
		}
	}
}


void EntityModel::addComponentNode(Lumix::Component cmp, int component_index)
{
	Node& node = getRoot().addChild(getComponentName(cmp), component_index + 3);
	node.m_getter = []() -> QVariant { return ""; };
	node.onCreateEditor = [this, cmp](QWidget* parent, const QStyleOptionViewItem&) {
		auto button = new QPushButton(" - ", parent);
		connect(button, &QPushButton::clicked, [this, cmp](){ m_editor.destroyComponent(cmp); });
		return button;
	};
	node.enablePeristentEditor();
	auto& descriptors = m_editor.getPropertyDescriptors(cmp.type);
	for (int j = 0; j < descriptors.size(); ++j)
	{
		auto* desc = descriptors[j];
		Node& child = node.addChild(desc->getName());
		child.m_getter = [desc, cmp, this]() -> QVariant
		{
			return this->get(cmp.entity, cmp.type, -1, desc);
		};
		switch (desc->getType())
		{
			case Lumix::IPropertyDescriptor::ARRAY:
				{
					addArrayProperty(child, static_cast<Lumix::IArrayDescriptor*>(desc), cmp);
					break;
				}
			case Lumix::IPropertyDescriptor::DECIMAL:
				{
					child.m_setter = [desc, cmp, this](const QVariant& value)
					{
						this->set(cmp.entity, cmp.type, -1, desc, value);
					};
					auto decimal_desc = static_cast<Lumix::IDecimalPropertyDescriptor*>(desc);
					if (decimal_desc->getStep() > 0)
					{
						DynamicObjectModel::setSliderEditor(child, decimal_desc->getMin(), decimal_desc->getMax(), decimal_desc->getStep());
						child.enablePeristentEditor();
					}
					break;
				}
			case Lumix::IPropertyDescriptor::RESOURCE:
				addResourceProperty(child, desc, cmp);
				break;
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
					this->set(cmp.entity, cmp.type, -1, desc, value);
				};
				break;
			}
	}
	emit m_view.componentNodeCreated(node, cmp);
}

void EntityModel::addComponent(QWidget* widget, QPoint pos)
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
	for (int i = 0; i < lengthOf(component_map); i += 2)
	{
		combobox->addItem(component_map[i]);
	}
	combobox->connect(combobox, (void (QComboBox::*)(int))&QComboBox::activated, [this, combobox](int value) {
		for (int i = 0; i < lengthOf(component_map); i += 2)
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

void EntityModel::set(Lumix::Entity entity, uint32_t component_type, int index, Lumix::IPropertyDescriptor* desc, QVariant value)
{
	m_is_setting = true;
	Lumix::Component cmp = m_editor.getComponent(entity, component_type);
	ASSERT(cmp.isValid());
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
				Lumix::Vec3 v;
				v.x = color.redF();
				v.y = color.greenF();
				v.z = color.blueF();
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
	m_is_setting = false;
}

QVariant EntityModel::get(Lumix::Entity entity, uint32_t component_type, int index, Lumix::IPropertyDescriptor* desc)
{
	Lumix::Component cmp = m_editor.getComponent(entity, component_type);
	ASSERT(cmp.isValid());
	Lumix::StackAllocator<256> allocator;
	Lumix::OutputBlob stream(allocator);
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
				Lumix::Vec3 c;
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
		default:
			Q_ASSERT(false);
			break;
	}

	return QVariant();
}

