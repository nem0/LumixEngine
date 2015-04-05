#include "entity_model.h"
#include "editor/world_editor.h"
#include <qcombobox.h>
#include <qmessagebox.h>


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


EntityModel::EntityModel(Lumix::WorldEditor& editor, Lumix::Entity entity)
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


EntityModel::~EntityModel()
{
	m_editor.componentAdded().unbind<EntityModel, &EntityModel::onComponentAdded>(this);
	m_editor.componentDestroyed().unbind<EntityModel, &EntityModel::onComponentDestroyed>(this);
	m_editor.propertySet().unbind<EntityModel, &EntityModel::onPropertySet>(this);
	m_editor.getUniverse()->entityMoved().unbind<EntityModel, &EntityModel::onEntityPosition>(this);
}


void EntityModel::onComponentAdded(Lumix::Component component)
{
	int row = m_editor.getComponents(component.entity).size() + 2;
	QModelIndex parent_index = createIndex(0, 0, &getRoot());
	beginInsertRows(parent_index, row, row);
	addComponentNode(component);
	endInsertRows();
}


void EntityModel::onComponentDestroyed(Lumix::Component component)
{
	auto& cmps = m_editor.getComponents(component.entity);
	int row = cmps.indexOf(component) + 2;
	QModelIndex parent_index = createIndex(0, 0, &getRoot());
	beginRemoveRows(parent_index, row, row);
	delete getRoot().m_children[row];
	getRoot().m_children.removeAt(row);
	endRemoveRows();
}


void EntityModel::onPropertySet(Lumix::Component component, const Lumix::IPropertyDescriptor& descriptor)
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

	m_editor.getUniverse()->entityMoved().bind<EntityModel, &EntityModel::onEntityPosition>(this);
}

void EntityModel::onEntityPosition(const Lumix::Entity& entity)
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


void EntityModel::addComponentNode(Lumix::Component cmp)
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

void EntityModel::set(Lumix::Component cmp, int index, Lumix::IPropertyDescriptor* desc, QVariant value)
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

QVariant EntityModel::get(Lumix::Component cmp, int index, Lumix::IPropertyDescriptor* desc)
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

Lumix::Entity EntityModel::getEntity() const
{
	return m_entity;
}
