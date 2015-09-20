#include "entity_model.h"
#include "core/crc32.h"
#include "core/stack_allocator.h"
#include "core/math_utils.h"
#include "core/path.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/property_descriptor.h"
#include "property_view.h"
#include "universe/universe.h"
#include <qapplication.h>
#include <qboxlayout.h>
#include <qcombobox.h>
#include <qfiledialog.h>
#include <qlineedit.h>
#include <qmessagebox.h>
#include <qmimedata.h>
#include <qpushbutton.h>


static const int COMPONENT_OFFSET = 4;


const char* EntityModel::getComponentName(Lumix::ComponentUID cmp) const
{
	for (int i = 0; i < m_editor.getEngine().getComponentTypesCount(); ++i)
	{
		if (cmp.type ==
			Lumix::crc32(m_editor.getEngine().getComponentTypeID(i)))
		{
			return m_editor.getEngine().getComponentTypeName(i);
		}
	}
	return "Unknown component";
}


EntityModel::EntityModel(PropertyView& view,
						 Lumix::WorldEditor& editor,
						 Lumix::Entity entity)
	: m_editor(editor)
	, m_view(view)
	, m_is_setting(false)
{
	m_entity = entity;
	getRoot().m_name = "Entity";
	getRoot().onCreateEditor = [this](QWidget* parent,
									  const QStyleOptionViewItem&)
	{
		auto button = new QPushButton("Add", parent);
		connect(button,
				&QPushButton::clicked,
				[this, button]()
				{
					addComponent(button, button->mapToGlobal(button->pos()));
				});
		return button;
	};
	getRoot().enablePeristentEditor();
	addNameProperty();
	addPositionProperty();

	Lumix::WorldEditor::ComponentList& cmps = m_editor.getComponents(m_entity);
	for (int i = 0; i < cmps.size(); ++i)
	{
		Lumix::ComponentUID cmp = cmps[i];
		addComponentNode(cmp, i);
	}

	m_editor.getUniverse()
		->entityDestroyed()
		.bind<EntityModel, &EntityModel::onEntityDestroyed>(this);
	m_editor.universeDestroyed()
		.bind<EntityModel, &EntityModel::onUniverseDestroyed>(this);
	m_editor.propertySet().bind<EntityModel, &EntityModel::onPropertySet>(this);
	m_editor.componentAdded().bind<EntityModel, &EntityModel::onComponentAdded>(
		this);
	m_editor.componentDestroyed()
		.bind<EntityModel, &EntityModel::onComponentDestroyed>(this);
}


void EntityModel::reset(const QString& reason)
{
	beginResetModel();
	m_entity = Lumix::INVALID_ENTITY;
	for (int i = 0; i < getRoot().m_children.size(); ++i)
	{
		delete getRoot().m_children[i];
		getRoot().m_children.clear();
		getRoot().m_getter = [reason]() -> QVariant
		{
			return reason;
		};
	}
	endResetModel();
}


void EntityModel::onEntityDestroyed(Lumix::Entity entity)
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
	m_editor.getUniverse()
		->entityDestroyed()
		.unbind<EntityModel, &EntityModel::onEntityDestroyed>(this);
	m_editor.universeDestroyed()
		.unbind<EntityModel, &EntityModel::onUniverseDestroyed>(this);
	m_editor.componentAdded()
		.unbind<EntityModel, &EntityModel::onComponentAdded>(this);
	m_editor.componentDestroyed()
		.unbind<EntityModel, &EntityModel::onComponentDestroyed>(this);
	m_editor.propertySet().unbind<EntityModel, &EntityModel::onPropertySet>(
		this);
	m_editor.getUniverse()
		->entityTransformed()
		.unbind<EntityModel, &EntityModel::onEntityPosition>(this);
}


void EntityModel::onComponentAdded(Lumix::ComponentUID component)
{
	if (m_entity == component.entity)
	{
		auto& cmps = m_editor.getComponents(component.entity);
		int row = cmps.indexOf(component) + COMPONENT_OFFSET;
		QModelIndex parent_index = createIndex(0, 0, &getRoot());
		beginInsertRows(parent_index, row, row);
		addComponentNode(component, row - COMPONENT_OFFSET);
		endInsertRows();
	}
}


void EntityModel::onComponentDestroyed(Lumix::ComponentUID component)
{
	if (component.entity == m_entity)
	{
		auto& cmps = m_editor.getComponents(component.entity);
		int row = cmps.indexOf(component) + COMPONENT_OFFSET;
		QModelIndex parent_index = createIndex(0, 0, &getRoot());
		beginRemoveRows(parent_index, row, row);
		delete getRoot().m_children[row];
		getRoot().m_children.removeAt(row);
		endRemoveRows();
	}
}


void EntityModel::onPropertySet(Lumix::ComponentUID component,
								const Lumix::IPropertyDescriptor& descriptor)
{
	if (component.entity == m_entity && !m_is_setting)
	{
		auto& cmps = m_editor.getComponents(component.entity);
		for (int i = 0; i < cmps.size(); ++i)
		{
			if (cmps[i].type == component.type)
			{
				auto& descriptors =
					m_editor.getEngine().getPropertyDescriptors(component.type);
				auto* node = getRoot().m_children[i + COMPONENT_OFFSET];
				for (int j = 0; j < node->m_children.size(); ++j)
				{
					if (descriptors[j] == &descriptor)
					{
						QModelIndex index =
							createIndex(j, 1, node->m_children[j]);
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
	name_node.m_getter = [this]() -> QVariant
	{
		return m_editor.getUniverse()->getEntityName(m_entity);
	};
	name_node.m_setter = [this](const QVariant& value)
	{
		if (m_editor.getUniverse()->nameExists(
				value.toString().toLatin1().data()))
		{
			QMessageBox::warning(nullptr,
								 "Warning",
								 "Entity with this name already exists!",
								 QMessageBox::Ok);
		}
		else
		{
			m_editor.setEntityName(m_entity,
								   value.toString().toLatin1().data());
		}
	};
}


void EntityModel::setEntityScale(float value)
{
	m_editor.setEntitiesScales(&m_entity, &value, 1);
}


void EntityModel::setEntityRotation(int index, float value)
{
	auto axis_angle = getUniverse()->getRotation(m_entity).getAxisAngle();
	((float*)&axis_angle)[index] = value;
	axis_angle.axis.normalize();
	Lumix::Quat rot(axis_angle.axis, axis_angle.angle);
	m_editor.setEntitiesRotations(&m_entity, &rot, 1);
}


void EntityModel::setEntityPosition(int index, float value)
{
	Lumix::Vec3 v = getUniverse()->getPosition(m_entity);
	((float*)&v)[index] = value;
	m_editor.setEntitiesPositions(&m_entity, &v, 1);
}


Lumix::Universe* EntityModel::getUniverse()
{
	return m_editor.getUniverse();
}


void EntityModel::addPositionProperty()
{
	Node& position_node = getRoot().addChild("position");
	position_node.m_getter = [this]() -> QVariant
	{
		Lumix::Vec3 pos = getUniverse()->getPosition(m_entity);
		return QString("%1; %2; %3")
			.arg(pos.x, 0, 'f', 6)
			.arg(pos.y, 0, 'f', 6)
			.arg(pos.z, 0, 'f', 6);
	};

	Node& x_node = position_node.addChild("x");
	x_node.m_getter = [this]() -> QVariant
	{
		return getUniverse()->getPosition(m_entity).x;
	};
	x_node.m_setter = [this](const QVariant& value)
	{
		setEntityPosition(0, value.toFloat());
	};
	Node& y_node = position_node.addChild("y");
	y_node.m_getter = [this]() -> QVariant
	{
		return getUniverse()->getPosition(m_entity).y;
	};
	y_node.m_setter = [this](const QVariant& value)
	{
		setEntityPosition(1, value.toFloat());
	};
	Node& z_node = position_node.addChild("z");
	z_node.m_getter = [this]() -> QVariant
	{
		return getUniverse()->getPosition(m_entity).z;
	};
	z_node.m_setter = [this](const QVariant& value)
	{
		setEntityPosition(2, value.toFloat());
	};

	Node& rotation_node = getRoot().addChild("rotation");
	rotation_node.m_getter = [this]() -> QVariant
	{
		auto rot = getUniverse()->getRotation(m_entity).getAxisAngle();
		return QString("[%1; %2; %3] %4")
			.arg(rot.axis.x, 0, 'f', 6)
			.arg(rot.axis.y, 0, 'f', 6)
			.arg(rot.axis.z, 0, 'f', 6)
			.arg(Lumix::Math::radiansToDegrees(rot.angle), 0, 'f', 6);
	};

	{
		Node& x_node = rotation_node.addChild("x");
		x_node.m_getter = [this]() -> QVariant
		{
			return getUniverse()->getRotation(m_entity).getAxisAngle().axis.x;
		};
		x_node.m_setter = [this](const QVariant& value)
		{
			setEntityRotation(0, value.toFloat());
		};
		Node& y_node = rotation_node.addChild("y");
		y_node.m_getter = [this]() -> QVariant
		{
			return getUniverse()->getRotation(m_entity).getAxisAngle().axis.y;
		};
		y_node.m_setter = [this](const QVariant& value)
		{
			setEntityRotation(1, value.toFloat());
		};
		Node& z_node = rotation_node.addChild("z");
		z_node.m_getter = [this]() -> QVariant
		{
			return getUniverse()->getRotation(m_entity).getAxisAngle().axis.z;
		};
		z_node.m_setter = [this](const QVariant& value)
		{
			setEntityRotation(2, value.toFloat());
		};
		Node& angle_node = rotation_node.addChild("angle");
		angle_node.m_getter = [this]() -> QVariant
		{
			return Lumix::Math::radiansToDegrees(
				getUniverse()->getRotation(m_entity).getAxisAngle().angle);
		};
		angle_node.m_setter = [this](const QVariant& value)
		{
			setEntityRotation(3,
							  Lumix::Math::degreesToRadians(value.toFloat()));
		};
		DynamicObjectModel::setSliderEditor(angle_node, 0, 360, 5);
	}

	Node& scale_node = getRoot().addChild("scale");
	scale_node.m_getter = [this]() -> QVariant
	{
		return getUniverse()->getScale(m_entity);
	};
	scale_node.m_setter = [this](const QVariant& value)
	{
		setEntityScale(value.toFloat());
	};

	m_editor.getUniverse()
		->entityTransformed()
		.bind<EntityModel, &EntityModel::onEntityPosition>(this);
}

void EntityModel::onEntityPosition(Lumix::Entity entity)
{
	if (entity == m_entity)
	{
		QModelIndex index = createIndex(1, 1, getRoot().m_children[1]);
		QModelIndex index_x =
			createIndex(0, 1, getRoot().m_children[1]->m_children[0]);
		QModelIndex index_z =
			createIndex(2, 1, getRoot().m_children[1]->m_children[2]);
		emit dataChanged(index, index);
		emit dataChanged(index_x, index_z);
	}
}


void EntityModel::addFileProperty(Node& child,
								  Lumix::IPropertyDescriptor* desc,
								  Lumix::ComponentUID cmp,
								  bool is_array,
								  bool is_resource)
{
	child.m_setter = [is_array, &child, desc, cmp, this](const QVariant& value)
	{
		this->set(cmp.entity,
				  cmp.type,
				  is_array ? child.m_parent->getIndex() : -1,
				  desc,
				  value);
	};
	child.onSetModelData = [is_array, &child, desc, cmp, this](QWidget* editor)
	{
		const auto& children = editor->children();
		auto value = qobject_cast<QLineEdit*>(children[1])->text();
		this->set(cmp.entity,
				  cmp.type,
				  is_array ? child.m_parent->getIndex() : -1,
				  desc,
				  value);
	};
	child.onDrop = [is_array, &child, desc, cmp, this](const QMimeData* data,
													   Qt::DropAction)
	{
		QList<QUrl> urls = data->urls();
		Q_ASSERT(urls.size() < 2);
		if (urls.size() == 1)
		{
			char rel_path[Lumix::MAX_PATH_LENGTH];
			Lumix::Path path(urls[0].toLocalFile().toLatin1().data());
			m_editor.getRelativePath(rel_path, Lumix::MAX_PATH_LENGTH, path);
			this->set(cmp.entity,
					  cmp.type,
					  is_array ? child.m_parent->getIndex() : -1,
					  desc,
					  rel_path);
			return true;
		}
		return false;
	};
	child.onCreateEditor = [is_array, &child, is_resource, desc, cmp, this](
		QWidget* parent, const QStyleOptionViewItem&) -> QWidget*
	{
		QWidget* widget = new QWidget(parent);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		QLineEdit* edit = new QLineEdit(widget);
		layout->addWidget(edit);
		QPushButton* button = new QPushButton("Browse", widget);
		connect(button,
				&QPushButton::clicked,
				[edit, parent, this]()
				{
					auto value = QFileDialog::getOpenFileName();
					if (!value.isEmpty())
					{
						char rel_path[Lumix::MAX_PATH_LENGTH];
						Lumix::Path path(value.toLatin1().data());
						m_editor.getRelativePath(
							rel_path, Lumix::MAX_PATH_LENGTH, path);
						edit->setText(rel_path);
					}
				});
		layout->addWidget(button);
		if (is_resource)
		{
			QPushButton* go_button = new QPushButton("->", widget);
			connect(go_button,
					&QPushButton::clicked,
					[edit, this]()
					{
						m_view.setSelectedResourceFilename(
							edit->text().toLatin1().data());
					});
			layout->addWidget(go_button);
		}
		layout->setContentsMargins(0, 0, 0, 0);
		edit->setText(this->get(cmp.entity,
								cmp.type,
								is_array ? child.m_parent->getIndex() : -1,
								desc)
						  .toString());
		return widget;
	};
}


QModelIndex EntityModel::getNodeIndex(Node& node)
{
	int row = node.m_parent ? node.m_parent->m_children.indexOf(&node) : 0;
	return createIndex(row, 0, &node);
}


void EntityModel::removeArrayItem(Node& node,
					 Lumix::IArrayDescriptor* array_desc,
					 Lumix::ComponentUID cmp)
{
	int row = node.getIndex();
	beginRemoveRows(getIndex(*node.m_parent), row, row);
	m_editor.removeArrayPropertyItem(cmp, row, *array_desc);
	node.m_parent->removeChild(node);
	endRemoveRows();
}


void EntityModel::addArrayItem(Node& parent,
							   Lumix::IArrayDescriptor* array_desc,
							   Lumix::ComponentUID cmp)
{
	Node& array_item_node =
		parent.addChild(QString::number(parent.m_children.size()));
	array_item_node.onCreateEditor = [this, &array_item_node, cmp, array_desc](
		QWidget* parent, const QStyleOptionViewItem&) -> QWidget*
	{
		auto* widget = new QWidget(parent);
		auto* layout = new QHBoxLayout(widget);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addStretch();
		auto* button = new QPushButton(" - ", widget);
		layout->addWidget(button);

		button->connect(button,
						&QPushButton::clicked,
						[this, &array_item_node, cmp, array_desc]()
						{
							removeArrayItem(array_item_node, array_desc, cmp);
						});

		return widget;
	};
	array_item_node.m_setter = [](const QVariant&) {};
	array_item_node.enablePeristentEditor();
	array_item_node.m_getter = []() -> QVariant
	{
		return "";
	};
	for (int l = 0; l < array_desc->getChildren().size(); ++l)
	{
		auto* array_item_property_desc = array_desc->getChildren()[l];

		addPropertyNode(array_item_node, array_item_property_desc, cmp, true);
	}
}


void EntityModel::addArrayProperty(Node& child,
								   Lumix::IArrayDescriptor* array_desc,
								   Lumix::ComponentUID cmp)
{
	child.onCreateEditor = [this, &child, cmp, array_desc](
		QWidget* parent, const QStyleOptionViewItem&)
	{
		auto button = new QPushButton(" + ", parent);
		button->connect(button,
						&QPushButton::clicked,
						[this, &child, cmp, array_desc]()
						{
							QModelIndex parent_index = getNodeIndex(child);
							int row = array_desc->getCount(cmp);
							beginInsertRows(parent_index, row, row);

							array_desc->addArrayItem(cmp, -1);
							addArrayItem(child, array_desc, cmp);

							endInsertRows();

						});
		return button;
	};
	child.m_setter = [](const QVariant&)
	{
	};
	child.m_is_persistent_editor = true;
	for (int k = 0; k < array_desc->getCount(cmp); ++k)
	{
		addArrayItem(child, array_desc, cmp);
	}
}


void EntityModel::addComponentNode(Lumix::ComponentUID cmp, int component_index)
{
	Node& node = getRoot().addChild(getComponentName(cmp),
									component_index + COMPONENT_OFFSET);
	node.m_getter = []() -> QVariant
	{
		return "";
	};
	node.onCreateEditor = [this, cmp](QWidget* parent,
									  const QStyleOptionViewItem&)
	{
		auto widget = new QWidget(parent);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addStretch(1);
		auto button = new QPushButton("Remove", widget);
		connect(button,
				&QPushButton::clicked,
				[this, cmp]()
				{
					m_editor.destroyComponent(cmp);
				});
		layout->addWidget(button);
		return widget;
	};
	node.enablePeristentEditor();
	auto& descriptors = m_editor.getEngine().getPropertyDescriptors(cmp.type);
	for (int j = 0; j < descriptors.size(); ++j)
	{
		auto* desc = descriptors[j];
		
		addPropertyNode(node, desc, cmp, false);
	}
	emit m_view.componentNodeCreated(node, cmp);
}


void EntityModel::addPropertyNode(Node& node,
								  Lumix::IPropertyDescriptor* desc,
								  Lumix::ComponentUID cmp,
								  bool is_array)
{
	Node& child = node.addChild(desc->getName());
	child.m_getter = [is_array, &node, desc, cmp, this]() -> QVariant
	{
		return this->get(
			cmp.entity, cmp.type, is_array ? node.getIndex() : -1, desc);
	};
	switch (desc->getType())
	{
		case Lumix::IPropertyDescriptor::ARRAY:
		{
			ASSERT(!is_array); // subarrays not supported
			addArrayProperty(
				child, static_cast<Lumix::IArrayDescriptor*>(desc), cmp);
			break;
		}
		case Lumix::IPropertyDescriptor::DECIMAL:
		{
			child.m_setter =
				[is_array, &node, desc, cmp, this](const QVariant& value)
			{
				this->set(cmp.entity,
						  cmp.type,
						  is_array ? node.getIndex() : -1,
						  desc,
						  value);
			};
			auto decimal_desc =
				static_cast<Lumix::IDecimalPropertyDescriptor*>(desc);
			if (decimal_desc->getStep() > 0)
			{
				DynamicObjectModel::setSliderEditor(child,
													decimal_desc->getMin(),
													decimal_desc->getMax(),
													decimal_desc->getStep());
				child.enablePeristentEditor();
			}
			break;
		}
		case Lumix::IPropertyDescriptor::FILE:
			addFileProperty(
				child, desc, cmp, is_array, false);
			break;
		case Lumix::IPropertyDescriptor::RESOURCE:
			addFileProperty(
				child, desc, cmp, is_array, true);
			break;
		case Lumix::IPropertyDescriptor::VEC3:
		{
			ASSERT(!is_array); // vec3s in arrays are not supported
			Node& x_node = child.addChild("x");
			x_node.m_getter = [desc, cmp]() -> QVariant
			{
				return desc->getValue<Lumix::Vec3>(cmp).x;
			};
			x_node.m_setter = [desc, cmp](const QVariant& value)
			{
				Lumix::Vec3 v = desc->getValue<Lumix::Vec3>(cmp);
				v.x = value.toFloat();
				desc->setValue(cmp, v);
			};
			Node& y_node = child.addChild("y");
			y_node.m_getter = [desc, cmp]() -> QVariant
			{
				return desc->getValue<Lumix::Vec3>(cmp).y;
			};
			y_node.m_setter = [desc, cmp](const QVariant& value)
			{
				Lumix::Vec3 v = desc->getValue<Lumix::Vec3>(cmp);
				v.y = value.toFloat();
				desc->setValue(cmp, v);
			};
			Node& z_node = child.addChild("z");
			z_node.m_getter = [desc, cmp]() -> QVariant
			{
				return desc->getValue<Lumix::Vec3>(cmp).z;
			};
			z_node.m_setter = [desc, cmp](const QVariant& value)
			{
				Lumix::Vec3 v = desc->getValue<Lumix::Vec3>(cmp);
				v.z = value.toFloat();
				desc->setValue(cmp, v);
			};
			break;
		}
		case Lumix::IPropertyDescriptor::VEC4:
		{
			ASSERT(!is_array); // vec4s in arrays are not supported
			Node& x_node = child.addChild("x");
			x_node.m_getter = [desc, cmp]() -> QVariant
			{
				return desc->getValue<Lumix::Vec4>(cmp).x;
			};
			x_node.m_setter = [desc, cmp](const QVariant& value)
			{
				Lumix::Vec4 v = desc->getValue<Lumix::Vec4>(cmp);
				v.x = value.toFloat();
				desc->setValue(cmp, v);
			};
			Node& y_node = child.addChild("y");
			y_node.m_getter = [desc, cmp]() -> QVariant
			{
				return desc->getValue<Lumix::Vec4>(cmp).y;
			};
			y_node.m_setter = [desc, cmp](const QVariant& value)
			{
				Lumix::Vec4 v = desc->getValue<Lumix::Vec4>(cmp);
				v.y = value.toFloat();
				desc->setValue(cmp, v);
			};
			Node& z_node = child.addChild("z");
			z_node.m_getter = [desc, cmp]() -> QVariant
			{
				return desc->getValue<Lumix::Vec4>(cmp).z;
			};
			z_node.m_setter = [desc, cmp](const QVariant& value)
			{
				Lumix::Vec4 v = desc->getValue<Lumix::Vec4>(cmp);
				v.z = value.toFloat();
				desc->setValue(cmp, v);
			};
			Node& w_node = child.addChild("w");
			w_node.m_getter = [desc, cmp]() -> QVariant
			{
				return desc->getValue<Lumix::Vec4>(cmp).w;
			};
			w_node.m_setter = [desc, cmp](const QVariant& value)
			{
				Lumix::Vec4 v = desc->getValue<Lumix::Vec4>(cmp);
				v.w = value.toFloat();
				desc->setValue(cmp, v);
			};
			break;
		}
		default:
			child.m_setter =
				[is_array, &node, desc, cmp, this](const QVariant& value)
			{
				this->set(cmp.entity,
						  cmp.type,
						  is_array ? node.getIndex() : -1,
						  desc,
						  value);
			};
			break;
	}
}


void EntityModel::addComponent(QWidget* widget, QPoint pos)
{
	struct CB : public QComboBox
	{
	public:
		CB(QWidget* parent)
			: QComboBox(parent)
		{
		}

		virtual void hidePopup() override { deleteLater(); }
	};

	CB* combobox = new CB(widget);
	for (int i = 0; i < m_editor.getEngine().getComponentTypesCount(); ++i)
	{
		combobox->addItem(m_editor.getEngine().getComponentTypeName(i));
	}
	combobox->connect(
		combobox,
		(void (QComboBox::*)(int)) & QComboBox::activated,
		[this, combobox](int value)
		{
			for (int i = 0; i < m_editor.getEngine().getComponentTypesCount();
				 ++i)
			{
				if (combobox->itemText(value) ==
					m_editor.getEngine().getComponentTypeName(i))
				{
					if (!m_editor.getComponent(
									 m_entity,
									 Lumix::crc32(m_editor.getEngine()
													  .getComponentTypeID(i)))
							 .isValid())
					{
						m_editor.addComponent(Lumix::crc32(
							m_editor.getEngine().getComponentTypeID(i)));
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


void EntityModel::set(Lumix::Entity entity,
					  uint32_t component_type,
					  int index,
					  Lumix::IPropertyDescriptor* desc,
					  QVariant value)
{
	m_is_setting = true;
	Lumix::ComponentUID cmp = m_editor.getComponent(entity, component_type);
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
			m_editor.setProperty(
				cmp.type, index, *desc, tmp.data(), tmp.length());
		}
		break;
		default:
			Q_ASSERT(false);
			break;
	}
	m_is_setting = false;
}

QVariant EntityModel::get(Lumix::Entity entity,
						  uint32_t component_type,
						  int index,
						  Lumix::IPropertyDescriptor* desc)
{
	Lumix::ComponentUID cmp = m_editor.getComponent(entity, component_type);
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
		case Lumix::IPropertyDescriptor::VEC4:
		{
			Lumix::Vec4 v;
			input.read(v);
			return QString("%1; %2; %3; %4").arg(v.x).arg(v.y).arg(v.z).arg(v.w);
		}
		case Lumix::IPropertyDescriptor::STRING:
		case Lumix::IPropertyDescriptor::RESOURCE:
		case Lumix::IPropertyDescriptor::FILE:
		{
			return (const char*)stream.getData();
		}
		case Lumix::IPropertyDescriptor::ARRAY:
			return QString("%1 members")
				.arg(
					static_cast<Lumix::IArrayDescriptor*>(desc)->getCount(cmp));
		default:
			Q_ASSERT(false);
			break;
	}

	return QVariant();
}
