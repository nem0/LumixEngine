#include "entity_list.h"
#include "ui_entity_list.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "universe/entity.h"


class EntityListModel : public QAbstractItemModel
{
	public:
		EntityListModel() 
		{
			m_universe = NULL;
		}

		virtual QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override
		{
			if(role == Qt::DisplayRole)
			{
				switch(section)
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
		
		
		virtual QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override
		{
			/*if(!hasIndex(row, column, parent))
			{
				return QModelIndex();
			}*/

			return createIndex(row, column);
		}
		
		
		virtual QModelIndex parent(const QModelIndex& index) const override
		{
			return QModelIndex();
		}
		
		
		virtual int rowCount(const QModelIndex& parent_index) const override
		{
			return parent_index.isValid() ? 0 : m_entities.size();
		}
		
		
		virtual int columnCount(const QModelIndex&) const override
		{
			return 1;
		}


		virtual QVariant data(const QModelIndex& index, int role) const override
		{
			return index.isValid() && role == Qt::DisplayRole ? m_entities[index.row()].index : QVariant();
		}


		void setUniverse(Lumix::Universe* universe)
		{
			if(m_universe)
			{
				m_universe->entityCreated().unbind<EntityListModel, &EntityListModel::onEntityCreated>(this);
				m_universe->entityDestroyed().unbind<EntityListModel, &EntityListModel::onEntityDestroyed>(this);
			}
			m_entities.clear();
			m_universe = universe;
			if(m_universe)
			{
				m_universe->entityCreated().bind<EntityListModel, &EntityListModel::onEntityCreated>(this);
				m_universe->entityDestroyed().bind<EntityListModel, &EntityListModel::onEntityDestroyed>(this);
				Lumix::Entity e = m_universe->getFirstEntity();
				while(e.isValid())
				{
					m_entities.push(e);
					e = m_universe->getNextEntity(e);
				}
			}
			if(m_universe)
			{
				emit dataChanged(createIndex(0, 0), createIndex(m_entities.size(), 0));
			}
		}

	
	private:
		void onEntityCreated(Lumix::Entity& entity)
		{
			m_entities.push(entity);
			emit dataChanged(createIndex(0, 0), createIndex(m_entities.size(), 0));
		}

		void onEntityDestroyed(Lumix::Entity& entity)
		{
			m_entities.eraseItem(entity);
			emit dataChanged(createIndex(0, 0), createIndex(m_entities.size(), 0));
		}

	private:
		Lumix::Universe* m_universe;
		Lumix::Array<Lumix::Entity> m_entities;
};


EntityList::EntityList(QWidget *parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::EntityList)
{
	m_universe = NULL;
	m_ui->setupUi(this);
	m_model = new EntityListModel;
	m_ui->entityList->setModel(m_model);
}


EntityList::~EntityList()
{
	m_editor->universeCreated().unbind<EntityList, &EntityList::onUniverseCreated>(this);
	m_editor->universeDestroyed().unbind<EntityList, &EntityList::onUniverseDestroyed>(this);
	m_editor->universeLoaded().unbind<EntityList, &EntityList::onUniverseLoaded>(this);

	delete m_ui;
	delete m_model;
}


void EntityList::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
	editor.universeCreated().bind<EntityList, &EntityList::onUniverseCreated>(this);
	editor.universeDestroyed().bind<EntityList, &EntityList::onUniverseDestroyed>(this);
	editor.universeLoaded().bind<EntityList, &EntityList::onUniverseLoaded>(this);
	m_model->setUniverse(editor.getEngine().getUniverse());
}


void EntityList::onUniverseCreated()
{
	m_universe = m_editor->getEngine().getUniverse();
	m_model->setUniverse(m_universe);
}


void EntityList::onUniverseLoaded()
{
	m_universe = m_editor->getEngine().getUniverse();
	m_model->setUniverse(m_universe);
}


void EntityList::onUniverseDestroyed()
{
	m_model->setUniverse(NULL);
	m_universe = NULL;
}

void EntityList::on_entityList_clicked(const QModelIndex &index)
{
	m_editor->selectEntity(Lumix::Entity(m_universe, m_model->data(index, Qt::DisplayRole).toInt()));
	TODO("select entity in the list when m_editor->entitySelected()");
}
