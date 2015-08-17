#include "entity_list.h"
#include "ui_entity_list.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "core/path_utils.h"
#include "core/stack_allocator.h"
#include "core/string.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "renderer/render_scene.h"
#include "universe/hierarchy.h"
#include "universe/universe.h"
#include <qmimedata.h>


static const uint32_t RENDERABLE_HASH = Lumix::crc32("renderable");


class SetParentEditorCommand : public Lumix::IEditorCommand
{
public:
	SetParentEditorCommand(Lumix::WorldEditor& editor,
						   Lumix::Hierarchy& hierarchy,
						   Lumix::Entity child,
						   Lumix::Entity parent)
		: m_new_parent(parent)
		, m_child(child)
		, m_old_parent(hierarchy.getParent(child))
		, m_hierarchy(hierarchy)
		, m_editor(editor)
	{
	}


	virtual void serialize(Lumix::JsonSerializer& serializer)
	{
		serializer.serialize("parent", m_new_parent);
		serializer.serialize("child", m_child);
	}


	virtual void deserialize(Lumix::JsonSerializer& serializer)
	{
		serializer.deserialize("parent", m_new_parent, 0);
		serializer.deserialize("child", m_child, 0);
		m_old_parent = m_hierarchy.getParent(m_child);
	}


	virtual void execute() override
	{
		m_hierarchy.setParent(m_child, m_new_parent);
	}


	virtual void undo() override
	{
		m_hierarchy.setParent(m_child, m_old_parent);
	}


	virtual bool merge(IEditorCommand&) override { return false; }


	virtual uint32_t getType() override
	{
		static const uint32_t hash = Lumix::crc32("set_entity_parent");
		return hash;
	}


private:
	Lumix::Entity m_child;
	Lumix::Entity m_new_parent;
	Lumix::Entity m_old_parent;
	Lumix::Hierarchy& m_hierarchy;
	Lumix::WorldEditor& m_editor;
};


class EntityListFilter : public QSortFilterProxyModel
{
public:
	EntityListFilter(QWidget* parent)
		: QSortFilterProxyModel(parent)
		, m_component(0)
		, m_is_update_enabled(true)
	{
	}
	void filterComponent(uint32_t component) { m_component = component; }
	void setUniverse(Lumix::Universe* universe)
	{
		m_universe = universe;
		invalidate();
	}
	void setWorldEditor(Lumix::WorldEditor& editor)
	{
		m_editor = &editor;
		editor.entityNameSet()
			.bind<EntityListFilter, &EntityListFilter::onEntityNameSet>(this);
	}
	void enableUpdate(bool enable) { m_is_update_enabled = enable; }

protected:
	virtual bool
	filterAcceptsRow(int source_row,
					 const QModelIndex& source_parent) const override
	{
		QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
		if (m_component == 0)
		{
			return sourceModel()->data(index).toString().contains(
				filterRegExp());
		}
		int entity_index = sourceModel()->data(index, Qt::UserRole).toInt();
		return m_editor->getComponent(Lumix::Entity(entity_index), m_component)
				   .isValid() &&
			   sourceModel()->data(index).toString().contains(filterRegExp());
	}

	void onEntityNameSet(Lumix::Entity, const char*)
	{
		if (m_is_update_enabled)
		{
			invalidate();
		}
	}

private:
	uint32_t m_component;
	Lumix::Universe* m_universe;
	bool m_is_update_enabled;
	Lumix::WorldEditor* m_editor;
};


class EntityListModel : public QAbstractItemModel
{
private:
	class EntityNode
	{
	public:
		EntityNode(EntityNode* parent, Lumix::Entity entity)
			: m_entity(entity)
			, m_parent(parent)
		{
		}

		~EntityNode()
		{
			for (int i = 0; i < m_children.size(); ++i)
			{
				delete m_children[i];
			}
		}

		EntityNode* getNode(Lumix::Entity entity)
		{
			if (m_entity == entity)
			{
				return this;
			}
			for (int i = 0; i < m_children.size(); ++i)
			{
				EntityNode* node = m_children[i]->getNode(entity);
				if (node)
				{
					return node;
				}
			}
			return nullptr;
		}

		bool removeEntity(Lumix::Entity entity)
		{
			if (m_entity == entity)
			{
				return true;
			}
			for (int i = 0; i < m_children.size(); ++i)
			{
				if (m_children[i]->removeEntity(entity))
				{
					m_children.remove(i);
					return false;
				}
			}
			return false;
		}

		EntityNode* m_parent;
		Lumix::Entity m_entity;
		QVector<EntityNode*> m_children;
	};

public:
	EntityListModel(QWidget* parent, EntityListFilter* filter)
		: QAbstractItemModel(parent)
	{
		m_root = nullptr;
		m_universe = nullptr;
		m_filter = filter;
		m_is_update_enabled = true;
	}

	~EntityListModel() { delete m_root; }


	void enableUpdate(bool enable) { m_is_update_enabled = enable; }


	virtual Qt::ItemFlags flags(const QModelIndex& index) const override
	{
		Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(index);

		if (index.isValid())
		{
			return Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled |
				   Qt::ItemIsEditable | defaultFlags;
		}
		else
		{
			return Qt::ItemIsDropEnabled | defaultFlags;
		}
	}


	virtual bool dropMimeData(const QMimeData* data,
							  Qt::DropAction action,
							  int row,
							  int column,
							  const QModelIndex& parent) override
	{
		if (action == Qt::IgnoreAction)
		{
			return true;
		}
		if (!data->hasFormat("application/lumix.entity"))
		{
			return false;
		}
		if (column > 0)
		{
			return false;
		}

		Lumix::Entity parent_entity(-1);
		if (row != -1)
		{
			parent_entity = parent.data(Qt::UserRole).toInt();
		}
		else if (parent.isValid())
		{
			parent_entity = parent.data(Qt::UserRole).toInt();
		}

		QByteArray encodedData = data->data("application/lumix.entity");
		QDataStream stream(&encodedData, QIODevice::ReadOnly);
		QStringList newItems;

		Lumix::Entity child(-1);
		if (!stream.atEnd())
		{
			stream >> child;
		}

		SetParentEditorCommand* command =
			m_engine->getWorldEditor()
				->getAllocator()
				.newObject<SetParentEditorCommand>(
					*m_engine->getWorldEditor(),
					*m_engine->getWorldEditor()->getHierarchy(),
					child,
					parent_entity);
		m_engine->getWorldEditor()->executeCommand(command);

		return false;
	}


	virtual Qt::DropActions supportedDropActions() const override
	{
		return Qt::CopyAction;
	}


	virtual QMimeData* mimeData(const QModelIndexList& indexes) const override
	{
		QMimeData* mimeData = new QMimeData();
		QByteArray encodedData;

		QDataStream stream(&encodedData, QIODevice::WriteOnly);

		stream << indexes.first().data(Qt::UserRole).toInt();

		mimeData->setData("application/lumix.entity", encodedData);
		return mimeData;
	}


	virtual QStringList mimeTypes() const override
	{
		QStringList types;
		types << "application/lumix.entity";
		return types;
	}


	virtual QVariant headerData(int section,
								Qt::Orientation,
								int role = Qt::DisplayRole) const override
	{
		if (role == Qt::DisplayRole)
		{
			switch (section)
			{
				case 0:
					return "ID";
					break;
				default:
					ASSERT(false);
					return QVariant();
			}
		}
		return QVariant();
	}


	virtual QModelIndex
	index(int row, int column, const QModelIndex& parent) const override
	{
		if (!hasIndex(row, column, parent))
		{
			return QModelIndex();
		}

		EntityNode* parentItem;

		if (!parent.isValid())
		{
			parentItem = m_root;
		}
		else
		{
			parentItem = static_cast<EntityNode*>(parent.internalPointer());
		}

		EntityNode* childItem = parentItem->m_children[row];
		if (childItem)
		{
			return createIndex(row, column, childItem);
		}
		return QModelIndex();
	}


	virtual QModelIndex parent(const QModelIndex& index) const override
	{
		if (!index.isValid() || !m_root)
		{
			return QModelIndex();
		}

		EntityNode* childItem =
			static_cast<EntityNode*>(index.internalPointer());
		EntityNode* parentItem = childItem->m_parent;

		if (parentItem == m_root)
		{
			return QModelIndex();
		}

		int row = parentItem->m_parent->m_children.indexOf(parentItem);
		return createIndex(row, 0, parentItem);
	}


	virtual int rowCount(const QModelIndex& parent) const override
	{
		if (parent.column() > 0 || !m_root)
		{
			return 0;
		}

		if (!parent.isValid())
		{
			return m_root->m_children.size();
		}
		EntityNode* node = static_cast<EntityNode*>(parent.internalPointer());
		return node->m_children.size();
	}


	virtual int columnCount(const QModelIndex&) const override { return 1; }


	virtual bool setData(const QModelIndex& index,
						 const QVariant& value,
						 int role = Qt::EditRole) override
	{
		if (index.isValid() && role == Qt::EditRole)
		{
			EntityNode* item =
				static_cast<EntityNode*>(index.internalPointer());
			switch (index.column())
			{
				case 0:
				{
					const QByteArray& name = value.toString().toLatin1();
					m_engine->getWorldEditor()->setEntityName(item->m_entity,
															  name.data());
					emit dataChanged(index, index);
					return true;
				}
				default:
					ASSERT(false);
			}
		}
		return QAbstractItemModel::setData(index, value, role);
	}


	virtual QVariant data(const QModelIndex& index, int role) const override
	{
		if (!index.isValid())
		{
			return QVariant("X");
		}

		EntityNode* item = static_cast<EntityNode*>(index.internalPointer());

		if (index.isValid() && role == Qt::DisplayRole)
		{
			Lumix::ComponentUID renderable =
				m_engine->getWorldEditor()->getComponent(item->m_entity,
														 RENDERABLE_HASH);
			const char* name =
				m_engine->getWorldEditor()->getUniverse()->getEntityName(
					item->m_entity);
			if (renderable.isValid())
			{
				const char* path =
					static_cast<Lumix::RenderScene*>(renderable.scene)
						->getRenderablePath(renderable.index);
				if (path && path[0] != 0)
				{
					char basename[Lumix::MAX_PATH_LENGTH];
					Lumix::PathUtils::getBasename(
						basename, Lumix::MAX_PATH_LENGTH, path);
					return name && name[0] != '\0'
							   ? QVariant(
									 QString("%1 - %2").arg(name).arg(basename))
							   : QVariant(QString("%1 - %2")
											  .arg(item->m_entity)
											  .arg(basename));
				}
			}
			return name && name[0] != '\0' ? QVariant(name)
										   : QVariant(item->m_entity);
		}
		else if (index.isValid() && role == Qt::UserRole)
		{
			return item->m_entity;
		}
		return role == 6 ? QVariant(QString("AAA")) : QVariant();
	}


	void setEngine(Lumix::Engine& engine) { m_engine = &engine; }


	void fillChildren(EntityNode* node)
	{
		Lumix::Array<Lumix::Hierarchy::Child>* children =
			m_engine->getWorldEditor()->getHierarchy()->getChildren(
				node->m_entity);
		if (children)
		{
			for (int i = 0; i < children->size(); ++i)
			{
				EntityNode* new_node = new EntityNode(
					node, Lumix::Entity((*children)[i].m_entity));
				node->m_children.push_back(new_node);
				fillChildren(new_node);
			}
		}
	}
	 

	void onParentSet(Lumix::Entity child, Lumix::Entity parent)
	{
		if (!m_root->m_children.empty())
		{
			EntityNode* node = m_root->getNode(child);
			node->m_parent->m_children.remove(
				node->m_parent->m_children.indexOf(node));

			EntityNode* parent_node = m_root->getNode(parent);
			if (!parent_node)
			{
				parent_node = m_root;
			}
			parent_node->m_children.push_back(node);
			node->m_parent = parent_node;

			if (m_is_update_enabled)
			{
				m_filter->invalidate();
			}
		}
	}


	void setUniverse(Lumix::Universe* universe)
	{
		m_filter->setUniverse(universe);
		if (m_universe)
		{
			m_universe->entityCreated()
				.unbind<EntityListModel, &EntityListModel::onEntityCreated>(
					this);
			m_universe->entityDestroyed()
				.unbind<EntityListModel, &EntityListModel::onEntityDestroyed>(
					this);
		}
		delete m_root;
		m_root = new EntityNode(nullptr, Lumix::INVALID_ENTITY);
		m_universe = universe;
		if (m_universe)
		{
			m_engine->getWorldEditor()
				->getHierarchy()
				->parentSet()
				.bind<EntityListModel, &EntityListModel::onParentSet>(this);
			m_universe->entityCreated()
				.bind<EntityListModel, &EntityListModel::onEntityCreated>(this);
			m_universe->entityDestroyed()
				.bind<EntityListModel, &EntityListModel::onEntityDestroyed>(
					this);
			Lumix::Entity e = m_universe->getFirstEntity();
			while (e >= 0)
			{
				Lumix::Entity parent =
					m_engine->getWorldEditor()->getHierarchy()->getParent(e);
				if (parent < 0)
				{
					EntityNode* node = new EntityNode(m_root, e);
					m_root->m_children.push_back(node);
					fillChildren(node);
				}
				e = m_universe->getNextEntity(e);
			}
		}
		if (m_universe && !m_root->m_children.empty() && m_is_update_enabled)
		{
			m_filter->invalidate();
		}
	}


private:
	void onEntityCreated(Lumix::Entity entity)
	{
		EntityNode* node = new EntityNode(m_root, entity);
		m_root->m_children.push_back(node);
		if (m_is_update_enabled)
		{
			m_filter->invalidate();
		}
	}

	void onEntityDestroyed(Lumix::Entity entity)
	{
		m_root->removeEntity(entity);
		if (m_is_update_enabled)
		{
			m_filter->invalidate();
		}
	}

private:
	Lumix::Universe* m_universe;
	Lumix::Engine* m_engine;
	EntityNode* m_root;
	EntityListFilter* m_filter;
	bool m_is_update_enabled;
};


EntityList::EntityList(QWidget* parent)
	: QDockWidget(parent)
	, m_ui(new Ui::EntityList)
{
	m_is_update_enabled = true;
	m_universe = nullptr;
	m_ui->setupUi(this);
	m_filter = new EntityListFilter(this);
	m_model = new EntityListModel(this, m_filter);
	m_filter->setDynamicSortFilter(true);
	m_filter->setSourceModel(m_model);
	m_ui->entityList->setModel(m_filter);
	m_ui->entityList->setDragEnabled(true);
	m_ui->entityList->setAcceptDrops(true);
	m_ui->entityList->setDropIndicatorShown(true);
}


void EntityList::shutdown()
{
	m_editor->universeCreated()
		.unbind<EntityList, &EntityList::onUniverseCreated>(this);
	m_editor->universeDestroyed()
		.unbind<EntityList, &EntityList::onUniverseDestroyed>(this);
	m_editor->universeLoaded()
		.unbind<EntityList, &EntityList::onUniverseLoaded>(this);
	m_editor->entitySelected()
		.unbind<EntityList, &EntityList::onEntitySelected>(this);
}


EntityList::~EntityList()
{
	delete m_ui;
}


void EntityList::enableUpdate(bool enable)
{
	m_is_update_enabled = enable;
	m_filter->enableUpdate(enable);
	m_model->enableUpdate(enable);
	m_filter->invalidate();
}


void EntityList::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
	editor.universeCreated().bind<EntityList, &EntityList::onUniverseCreated>(
		this);
	editor.universeDestroyed()
		.bind<EntityList, &EntityList::onUniverseDestroyed>(this);
	editor.universeLoaded().bind<EntityList, &EntityList::onUniverseLoaded>(
		this);
	m_universe = editor.getUniverse();
	m_model->setEngine(editor.getEngine());
	m_model->setUniverse(m_universe);
	m_filter->setSourceModel(m_model);
	m_filter->setWorldEditor(editor);
	m_ui->comboBox->clear();
	m_ui->comboBox->addItem("All");
	for (int i = 0; i < m_editor->getComponentTypesCount(); ++i)
	{
		m_ui->comboBox->addItem(m_editor->getComponentTypeName(i));
	}
	editor.entitySelected().bind<EntityList, &EntityList::onEntitySelected>(
		this);
}


void EntityList::fillSelection(const QModelIndex& parent,
							   QItemSelection* selection,
							   const Lumix::Array<Lumix::Entity>& entities)
{
	for (int i = 0, c = m_filter->rowCount(parent); i < c; ++i)
	{
		auto index = m_filter->index(i, 0, parent);
		auto entity_index = m_filter->data(index, Qt::UserRole).toInt();
		for (int j = entities.size() - 1; j >= 0; --j)
		{
			if (entity_index == entities[j])
			{
				selection->append(
					QItemSelectionRange(m_filter->index(i, 0, parent)));
				break;
			}
		}
		if (m_filter->rowCount(index) > 0)
		{
			fillSelection(index, selection, entities);
		}
	}
}


void EntityList::onEntitySelected(const Lumix::Array<Lumix::Entity>& entities)
{
	QItemSelection* selection = new QItemSelection();
	fillSelection(QModelIndex(), selection, entities);
	m_ui->entityList->selectionModel()->select(
		*selection,
		QItemSelectionModel::SelectionFlag::ClearAndSelect |
			QItemSelectionModel::SelectionFlag::Rows);
	delete selection;
}


void EntityList::onUniverseCreated()
{
	m_universe = m_editor->getUniverse();
	m_model->setUniverse(m_universe);
}


void EntityList::onUniverseLoaded()
{
	m_universe = m_editor->getUniverse();
	m_model->setUniverse(m_universe);
	if (m_is_update_enabled)
	{
		m_filter->invalidate();
	}
}


void EntityList::onUniverseDestroyed()
{
	m_model->setUniverse(nullptr);
	m_universe = nullptr;
}


void EntityList::on_entityList_clicked(const QModelIndex& index)
{
	Lumix::Entity e(m_filter->data(index, Qt::UserRole).toInt());
	m_editor->selectEntities(&e, 1);
}


void EntityList::on_comboBox_activated(const QString& arg1)
{
	for (int i = 0; i < m_editor->getComponentTypesCount(); ++i)
	{
		if (arg1 == m_editor->getComponentTypeName(i))
		{
			m_filter->filterComponent(Lumix::crc32(m_editor->getComponentTypeID(i)));
			if (m_is_update_enabled)
			{
				m_filter->invalidate();
			}
			return;
		}
	}
	m_filter->filterComponent(0);
	if (m_is_update_enabled)
	{
		m_filter->invalidate();
	}
}

void EntityList::on_nameFilterEdit_textChanged(const QString& arg1)
{
	QRegExp regExp(arg1);
	m_filter->setFilterRegExp(regExp);
}
