#include "assetbrowser.h"
#include "ui_assetbrowser.h"
#include <qfilesystemmodel.h>
#include "editor/editor_client.h"


AssetBrowser::AssetBrowser(QWidget *parent) :
	QDockWidget(parent),
	ui(new Ui::AssetBrowser)
{
	m_client = NULL;
	ui->setupUi(this);
	m_model = new QFileSystemModel;
	m_model->setRootPath(QDir::currentPath());
	QStringList filters;
	filters << "*.msh" << "*.unv" << "*.ani";
	m_model->setNameFilterDisables(false);
	m_model->setNameFilters(filters);
	ui->treeView->setModel(m_model);
	ui->treeView->setRootIndex(m_model->index(QDir::currentPath()));
	ui->treeView->hideColumn(1);
	ui->treeView->hideColumn(2);
	ui->treeView->hideColumn(3);
	ui->treeView->hideColumn(4);
}

AssetBrowser::~AssetBrowser()
{
	delete ui;
	delete m_model;
}

void AssetBrowser::on_treeView_doubleClicked(const QModelIndex &index)
{
	if(m_model->fileInfo(index).suffix() == "unv")
	{
		m_client->loadUniverse(m_model->filePath(index).toLower().toLatin1().data());
	}
}
