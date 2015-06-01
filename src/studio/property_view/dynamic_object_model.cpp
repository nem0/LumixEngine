#include "dynamic_object_model.h"
#include "core/vec4.h"
#include <qapplication.h>
#include <qcolordialog.h>
#include <qevent.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qpainter.h>
#include <qspinbox.h>


DynamicObjectItemDelegate::DynamicObjectItemDelegate(QWidget* parent) 
	: QStyledItemDelegate(parent) 
{}


void DynamicObjectItemDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
	auto* node = (DynamicObjectModel::Node*)index.internalPointer();
	if (node && node->onSetModelData)
	{
		node->onSetModelData(editor);
		return;
	}
	QStyledItemDelegate::setModelData(editor, model, index);
}


void DynamicObjectItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
	if (qobject_cast<QDoubleSpinBox*>(editor))
	{
		qobject_cast<QDoubleSpinBox*>(editor)->setValue(index.data().toFloat());
		return;
	}
	QStyledItemDelegate::setEditorData(editor, index);
}


bool DynamicObjectItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem&, const QModelIndex& index)
{
	if (event->type() == QEvent::MouseButtonRelease)
	{
		auto* node = (DynamicObjectModel::Node*)index.internalPointer();
		if (!node)
		{
			return false;
		}
		if (node->onClick && index.column() == 1)
		{
			QWidget* widget = qobject_cast<QWidget*>(parent());
			QPoint pos = widget->mapToGlobal(QPoint(static_cast<QMouseEvent*>(event)->x(), static_cast<QMouseEvent*>(event)->y()));
			node->onClick(widget, pos);
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


void DynamicObjectItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	if (index.column() == 1)
	{
		auto* node = (DynamicObjectModel::Node*)index.internalPointer();
		if (!node)
		{
			QStyledItemDelegate::paint(painter, option, index);
			return;
		}
		if (node->onPaint)
		{
			node->onPaint(painter, option);
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


QWidget* DynamicObjectItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	if (index.column() == 1)
	{
		auto node = (DynamicObjectModel::Node*)index.internalPointer();
		if (!node)
		{
			return QStyledItemDelegate::createEditor(parent, option, index);
		}
		if (node->onCreateEditor)
		{
			return node->onCreateEditor(parent, option);
		}
		else if (node->m_getter().type() == QMetaType::Bool)
		{
			return NULL;
		}
		else if (node->m_getter().type() == QMetaType::Float)
		{
			QDoubleSpinBox* input = new QDoubleSpinBox(parent);
			input->setMaximum(FLT_MAX);
			input->setMinimum(-FLT_MAX);
			connect(input, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [node](double value){
				node->m_setter(value);
			});
			input->setSingleStep(0.1);
			return input;
		}
	}
	return QStyledItemDelegate::createEditor(parent, option, index);
}


DynamicObjectModel::Node::~Node()
{
	for (auto child : m_children)
	{
		delete child;
	}
}


DynamicObjectModel::DynamicObjectModel()
{
	m_root = new Node("root", NULL, 0);
	Node* root = m_root;
	m_root->m_getter = [root]() -> QVariant { return ""; };
}


DynamicObjectModel::~DynamicObjectModel()
{
	delete m_root;
}


bool DynamicObjectModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int, int, const QModelIndex& parent)
{
	Node* node = (Node*)parent.internalPointer();
	if (node->onDrop)
	{
		return node->onDrop(data, action);
	}
	return false;
}


Qt::DropActions DynamicObjectModel::supportedDropActions() const
{
	return Qt::CopyAction | Qt::MoveAction;
}


QStringList DynamicObjectModel::mimeTypes() const
{
	QStringList types;
	types << "text/uri-list";
	return types;
}


void DynamicObjectModel::removeNode(Node& node)
{
	Q_ASSERT(node.m_parent);
	beginRemoveRows(getIndex(*node.m_parent), node.m_index, node.m_index);
	node.m_parent->m_children.removeAt(node.m_index);
	delete &node;
	endRemoveRows();
}


void DynamicObjectModel::childAboutToBeAdded(Node& node)
{
	beginInsertRows(getIndex(node), node.m_children.size(), node.m_children.size());
}


void DynamicObjectModel::childAdded()
{
	endInsertRows();
}


QModelIndex DynamicObjectModel::getIndex(Node& node)
{
	return createIndex(node.m_index, 0, &node);
}


QModelIndex DynamicObjectModel::index(int row, int column, const QModelIndex& parent) const
{
	Node* node = m_root;
	if (parent.isValid())
	{
		auto x = ((Node*)parent.internalPointer());
		if (row >= x->m_children.size())
			return QModelIndex();
		node = x->m_children[row];
	}
	return createIndex(row, column, node);
}


QModelIndex DynamicObjectModel::parent(const QModelIndex& child) const
{
	Node* node = (Node*)child.internalPointer();
	if (!node->m_parent)
	{
		return QModelIndex();
	}
	return createIndex(node->m_parent->m_index, 0, node->m_parent);
}


int DynamicObjectModel::rowCount(const QModelIndex& parent) const
{
	if (!parent.isValid())
		return 1;
	Node* node = (Node*)parent.internalPointer();
	return node->m_children.size();
}


int DynamicObjectModel::columnCount(const QModelIndex&) const
{
	return 2;
}


QVariant DynamicObjectModel::data(const QModelIndex& index, int role) const
{
	if (role == PersistentEditorRole)
	{
		Node* node = (Node*)index.internalPointer();
		return node->m_is_persistent_editor;
	}
	if (role == Qt::DecorationRole)
	{
		Node* node = (Node*)index.internalPointer();
		if (index.column() == 1 && node->m_decoration)
		{
			return node->m_decoration();
		}
	}
	else if (role == Qt::SizeHintRole)
	{
		Node* node = (Node*)index.internalPointer();
		if (index.column() == 1 && node->m_size_hint)
		{
			return node->m_size_hint();
		}
	}
	else if (role == Qt::DisplayRole)
	{
		Node* node = (Node*)index.internalPointer();
		if (index.column() == 0)
		{
			return node->m_name;
		}
		return node->m_getter();
	}
	return QVariant();
}


bool DynamicObjectModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (role == Qt::EditRole && index.column() == 1 && index.isValid())
	{
		Node* node = (Node*)index.internalPointer();
		if (node->m_setter)
		{
			node->m_setter(value);
			emit dataChanged(index, index);
		}
	}
	return false;
}


Qt::ItemFlags DynamicObjectModel::flags(const QModelIndex& index) const
{
	Node* node = (Node*)index.internalPointer();
	Qt::ItemFlags flags = QAbstractItemModel::flags(index);
	if (index.column() == 1 && node->m_setter)
	{
		flags |= Qt::ItemIsEditable;
	}
	if (index.column() == 1 && node->onDrop)
	{
		flags |= Qt::ItemIsDropEnabled;

	}
	return flags;
}


QVariant DynamicObjectModel::headerData(int section, Qt::Orientation, int role) const
{
	if (role == Qt::DisplayRole)
	{
		if (section == 0)
		{
			return "Name";
		}
		return "Value";
	}
	return QVariant();
}


void DynamicObjectModel::setSliderEditor(Node& node, float min, float max, float step)
{
	auto value = node.m_getter().toFloat();
	node.onCreateEditor = [&node, value, min, max, step](QWidget* parent, const QStyleOptionViewItem&) -> QWidget* {
		QWidget* widget = new QWidget(parent);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		layout->setContentsMargins(0, 0, 0, 0);
		auto slider = new QSlider(Qt::Orientation::Horizontal, widget);
		slider->setRange(min * 100, max * 100);
		slider->setSingleStep(step * 100);
		slider->setPageStep(step * 100);
		slider->setValue(value * 100);
		slider->setTracking(true);
		auto input = new QDoubleSpinBox(widget);
		input->setRange(min, max);
		input->setValue(value);
		input->setDecimals(2);
		input->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
		connect(slider, &QSlider::valueChanged, [&node, input](int value){
			node.m_setter(value * 0.01f);
			input->setValue(value * 0.01f);
		});
		connect(input, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [slider, node](double value){
			node.m_setter(value);
			slider->setValue((int)(value * 100));
		});
		layout->addWidget(input);
		layout->addWidget(slider);
		return widget;
	};
}

