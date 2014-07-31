#include "assetbrowser.h"
#include "ui_assetbrowser.h"
#include "file_system_watcher.h"
#include "core/crc32.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "editor/editor_server.h"
#include "engine/engine.h"
#include <qfilesystemmodel.h>
#include <qlistwidget.h>
#include <qmenu.h>
#include <qprocess.h>


struct ProcessInfo
{
	class QProcess* m_process;
	Lumix::string m_path;
};


void getDefaultFilters(QStringList& filters)
{
	filters << "*.msh" << "*.unv" << "*.ani" << "*.blend" << "*.tga" << "*.mat";
}


AssetBrowser::AssetBrowser(QWidget* parent) :
	QDockWidget(parent),
	m_ui(new Ui::AssetBrowser)
{
	m_watcher = FileSystemWatcher::create(QDir::currentPath().toLatin1().data());
	m_watcher->getCallback().bind<AssetBrowser, &AssetBrowser::onFileSystemWatcherCallback>(this);
	m_base_path = QDir::currentPath();
	m_server = NULL;
	m_ui->setupUi(this);
	m_model = new QFileSystemModel;
	m_model->setRootPath(QDir::currentPath());
	QStringList filters;
	getDefaultFilters(filters);
	m_model->setNameFilters(filters);
	m_model->setNameFilterDisables(false);
	m_ui->treeView->setModel(m_model);
	m_ui->treeView->setRootIndex(m_model->index(QDir::currentPath()));
	m_ui->treeView->hideColumn(1);
	m_ui->treeView->hideColumn(2);
	m_ui->treeView->hideColumn(3);
	m_ui->treeView->hideColumn(4);
	m_ui->listWidget->hide();
	connect(this, SIGNAL(fileChanged(const QString&)), this, SLOT(onFileChanged(const QString&)));
}

AssetBrowser::~AssetBrowser()
{
	delete m_ui;
	delete m_model;
}


void AssetBrowser::onFileSystemWatcherCallback(const char* path)
{
	emitFileChanged(path);
}


void AssetBrowser::emitFileChanged(const char* path)
{
	emit fileChanged(path);
}


void AssetBrowser::handleDoubleClick(const QFileInfo& file_info)
{
	const QString& suffix = file_info.suffix();
	QString file =file_info.filePath().toLower();
	if(suffix == "unv")
	{
		m_server->loadUniverse(file.toLatin1().data());
	}
	else if(suffix == "msh")
	{
		m_server->addEntity();
		m_server->addComponent(crc32("renderable"));
		m_server->setProperty("renderable", "source", file.toLatin1().data(), file.length());
	}
	else if(suffix == "ani")
	{
		m_server->addComponent(crc32("animable"));
		m_server->setProperty("animable", "preview", file.toLatin1().data(), file.length());
	}
}


void AssetBrowser::on_treeView_doubleClicked(const QModelIndex &index)
{
	ASSERT(m_model);
	handleDoubleClick(m_model->fileInfo(index));
}


void AssetBrowser::onFileChanged(const QString& path)
{
	if(QFileInfo(path).suffix() == "blend@")
	{
		QFileInfo file_info(path);
		QString base_name = file_info.absolutePath() + "/" + file_info.baseName() + ".blend";
		QFileInfo result_file_info(base_name);
		exportAnimation(result_file_info);
		exportModel(result_file_info);
	}
	else if(m_server)
	{
		m_server->getEngine().getResourceManager().reload(path.toLatin1().data());
	}
}


void fillList(QListWidget& widget, const QDir& dir, const QStringList& filters)
{
	QFileInfoList list = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);

	for(int i = 0, c = list.size(); i < c; ++i)
	{
		QString filename = list[i].fileName();
		QListWidgetItem* item = new QListWidgetItem(list[i].fileName());
		widget.addItem(item);
		item->setData(Qt::UserRole, list[i].filePath());
	}
	
	list = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);

	for(int i = 0, c = list.size(); i < c; ++i)
	{
		QString filename = list[i].fileName();
		fillList(widget, QDir(list[i].filePath()), filters);
	}
}


void AssetBrowser::on_searchInput_textEdited(const QString &arg1)
{
	if(arg1.length() == 0)
	{
		m_ui->listWidget->hide();
		m_ui->treeView->show();
	}
	else
	{
		QStringList filters;
		filters << QString("*") + arg1 + "*";
		m_ui->listWidget->show();
		m_ui->treeView->hide();
		QDir dir(QDir::currentPath());
		m_ui->listWidget->clear();
		fillList(*m_ui->listWidget, dir, filters);
	}
}


void AssetBrowser::on_listWidget_activated(const QModelIndex &index)
{
	QVariant user_data = m_ui->listWidget->item(index.row())->data(Qt::UserRole);
	QFileInfo info(user_data.toString());
	handleDoubleClick(info);
}

void AssetBrowser::on_exportFinished(int)
{
	QProcess* process = static_cast<QProcess*>(QObject::sender());
	QString s = process->readAll();;
	process->deleteLater();
	while(process->waitForReadyRead())
	{
		s += process->readAll();
	}
}

void AssetBrowser::exportAnimation(const QFileInfo& file_info)
{
	ProcessInfo process;
	process.m_path = file_info.path().toLatin1().data();
	process.m_process = new QProcess();
	//m_processes.push(process);
	QStringList list;
	list.push_back("/C");
	list.push_back("models\\export_anim.bat");
	list.push_back(file_info.absoluteFilePath().toLatin1().data());
	list.push_back(m_base_path.toLatin1().data());
	connect(process.m_process, (void (QProcess::*)(int))&QProcess::finished, this, &AssetBrowser::on_exportFinished);
	process.m_process->start("cmd.exe", list);
}

void AssetBrowser::exportModel(const QFileInfo& file_info)
{
	ProcessInfo process;
	process.m_path = file_info.path().toLatin1().data();
	process.m_process = new QProcess();
	//m_processes.push(process);
	QStringList list;
	list.push_back("/C");
	list.push_back("models\\export_mesh.bat");
	list.push_back(file_info.absoluteFilePath().toLatin1().data());
	list.push_back(m_base_path.toLatin1().data());
	connect(process.m_process, (void (QProcess::*)(int))&QProcess::finished, this, &AssetBrowser::on_exportFinished);
	process.m_process->start("cmd.exe", list);
}

void AssetBrowser::on_treeView_customContextMenuRequested(const QPoint &pos)
{
	QMenu *menu = new QMenu("Item actions",NULL);
	const QModelIndex& index = m_ui->treeView->indexAt(pos);
	const QFileInfo& file_info = m_model->fileInfo(index);
	if(file_info.suffix() == "blend")
	{
		QAction* export_anim_action = new QAction("Export Animation", menu);
		QAction* export_model_action = new QAction("Export Model", menu);
		menu->addAction(export_anim_action);
		menu->addAction(export_model_action);
		QAction* action = menu->exec(mapToGlobal(pos));
		if(action == export_anim_action)
		{
			exportAnimation(file_info);
		}
		else if(action == export_model_action)
		{
			exportModel(file_info);
		}
	}
}

void AssetBrowser::on_filterComboBox_currentTextChanged(const QString&)
{
	QStringList filters;
	if(m_ui->filterComboBox->currentText() == "All")
	{
		getDefaultFilters(filters);
	}
	else if(m_ui->filterComboBox->currentText() == "Mesh")
	{
		filters << "*.msh";
	}
	else if(m_ui->filterComboBox->currentText() == "Material")
	{
		filters << "*.mat";
	}
	m_model->setNameFilters(filters);
}
