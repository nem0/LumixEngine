#include "entity_template_list.h"
#include "core/crc32.h"
#include "ui_entity_template_list.h"
#include "editor/entity_template_system.h"
#include "editor/world_editor.h"


EntityTemplateList::EntityTemplateList() 
	: QDockWidget(nullptr)
	, m_ui(new Ui::EntityTemplateList)
{
	m_ui->setupUi(this);
	m_editor = nullptr;
}

EntityTemplateList::~EntityTemplateList()
{
	if (m_editor)
	{
		m_editor->getEntityTemplateSystem().updated().unbind<EntityTemplateList, &EntityTemplateList::onSystemUpdated>(this);
	}
	delete m_ui;
}


void EntityTemplateList::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
	m_editor->getEntityTemplateSystem().updated().bind<EntityTemplateList, &EntityTemplateList::onSystemUpdated>(this);
	onSystemUpdated();
}


void EntityTemplateList::onSystemUpdated()
{
	Lumix::Array<Lumix::string>& template_names =
		m_editor->getEntityTemplateSystem().getTemplateNames();
	m_ui->templateList->clear();
	for (int i = 0, c = template_names.size(); i < c; ++i)
	{
		m_ui->templateList->insertItem(i, template_names[i].c_str());
	}
}


void EntityTemplateList::on_templateList_doubleClicked(const QModelIndex &index)
{
	Lumix::Vec3 pos = m_editor->getCameraRaycastHit();
	m_editor->getEntityTemplateSystem().createInstance(m_ui->templateList->item(index.row())->text().toLatin1().data(), pos);
}


void EntityTemplateList::instantiateTemplate()
{
	instantiateTemplateAt(m_editor->getCameraRaycastHit());
}


int EntityTemplateList::getTemplate() const
{
	if (m_ui->templateList->currentIndex().row() >= 0)
	{
		uint32_t hash = Lumix::crc32(m_ui->templateList->item(m_ui->templateList->currentIndex().row())->text().toLatin1().data());
		return m_editor->getEntityTemplateSystem().getInstances(hash)[0];
	}
	return Lumix::INVALID_ENTITY;
}


void EntityTemplateList::instantiateTemplateAt(const Lumix::Vec3& pos)
{
	if (m_ui->templateList->currentIndex().row() >= 0)
	{
		m_editor->getEntityTemplateSystem().createInstance(m_ui->templateList->item(m_ui->templateList->currentIndex().row())->text().toLatin1().data(), pos);
	}
}


