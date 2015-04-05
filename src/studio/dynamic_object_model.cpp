#include "dynamic_object_model.h"


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
	if (role == Qt::DisplayRole)
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
	if (index.column() == 1 && node->m_setter)
	{
		return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
	}
	return QAbstractItemModel::flags(index);
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

