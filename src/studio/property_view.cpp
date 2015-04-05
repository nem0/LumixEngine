#include "property_view.h"
#include "ui_property_view.h"
#include "assetbrowser.h"
#include "core/crc32.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "entity_list.h"
#include "entity_template_list.h"
#include "property_view/dynamic_object_model.h"
#include "property_view/entity_model.h"
#include "property_view/resource_model.h"
#include <qitemdelegate.h>


PropertyView::PropertyView(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::PropertyView)
	, m_selected_entity(Lumix::Entity::INVALID)
{
	m_ui->setupUi(this);
}


PropertyView::~PropertyView()
{
	m_world_editor->entitySelected().unbind<PropertyView, &PropertyView::onEntitySelected>(this);
	delete m_ui;
}


void PropertyView::setModel(QAbstractItemModel* model, QAbstractItemDelegate* delegate)
{
	delete m_ui->treeView->model();
	delete m_ui->treeView->itemDelegate();
	m_ui->treeView->setModel(model);
	if (delegate)
	{
		m_ui->treeView->setItemDelegate(delegate);
	}
	else
	{
		m_ui->treeView->setItemDelegate(new QItemDelegate(this));
	}
}


void PropertyView::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_world_editor = &editor;
	m_world_editor->universeDestroyed().bind<PropertyView, &PropertyView::onUniverseDestroyed>(this);
	m_world_editor->universeCreated().bind<PropertyView, &PropertyView::onUniverseCreated>(this);
	m_world_editor->getUniverse()->entityDestroyed().bind<PropertyView, &PropertyView::onEntityDestroyed>(this);
	m_world_editor->entitySelected().bind<PropertyView, &PropertyView::onEntitySelected>(this);
}


void PropertyView::onEntityDestroyed(const Lumix::Entity& entity)
{
	if (m_selected_entity == entity)
	{
		setModel(NULL, NULL);
	}
}


void PropertyView::onUniverseCreated()
{
	m_world_editor->getUniverse()->entityDestroyed().bind<PropertyView, &PropertyView::onEntityDestroyed>(this);
}


void PropertyView::onUniverseDestroyed()
{
	m_world_editor->getUniverse()->entityDestroyed().unbind<PropertyView, &PropertyView::onEntityDestroyed>(this);
	setModel(NULL, NULL);
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


void PropertyView::setSelectedResource(Lumix::Resource* resource)
{
	if (resource)
	{
		setModel(new ResourceModel(resource), NULL);
	}
}


void PropertyView::onEntitySelected(const Lumix::Array<Lumix::Entity>& e)
{
	setSelectedResource(NULL);
	m_selected_entity = e.empty() ? Lumix::Entity::INVALID : e[0];
	if (e.size() == 1 && e[0].isValid())
	{
		EntityModel* model = new EntityModel(*m_world_editor, m_selected_entity);
		setModel(model, new DynamicObjectItemDelegate(m_ui->treeView));
		m_ui->treeView->expandAll();
	}
}

