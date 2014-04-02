#include "assetbrowser.h"
#include "ui_assetbrowser.h"
#include <qfilesystemmodel.h>
#include "editor/editor_client.h"


AssetBrowser::AssetBrowser(QWidget *parent) :
	QDockWidget(parent),
	m_ui(new Ui::AssetBrowser)
{
	m_client = NULL;
	m_ui->setupUi(this);
	m_model = new QFileSystemModel;
	m_model->setRootPath(QDir::currentPath());
	QStringList filters;
	filters << "*.msh" << "*.unv" << "*.ani";
	m_model->setNameFilterDisables(false);
	m_model->setNameFilters(filters);
	m_ui->treeView->setModel(m_model);
	m_ui->treeView->setRootIndex(m_model->index(QDir::currentPath()));
	m_ui->treeView->hideColumn(1);
	m_ui->treeView->hideColumn(2);
	m_ui->treeView->hideColumn(3);
	m_ui->treeView->hideColumn(4);
}

AssetBrowser::~AssetBrowser()
{
	delete m_ui;
	delete m_model;
}

void AssetBrowser::on_treeView_doubleClicked(const QModelIndex &index)
{
	ASSERT(m_client && m_model);
	if(m_model->fileInfo(index).suffix() == "unv")
	{
		m_client->loadUniverse(m_model->filePath(index).toLower().toLatin1().data());
	}
}
