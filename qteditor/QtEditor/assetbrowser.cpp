#include "assetbrowser.h"
#include "ui_assetbrowser.h"
#include <qfilesystemmodel.h>
#include <qlistwidget.h>
#include "core/crc32.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "editor/editor_client.h"
#include "editor/editor_server.h"
#include "engine/engine.h"

#include <Windows.h>


// http://qualapps.blogspot.sk/2010/05/understanding-readdirectorychangesw_19.html
class FileSystemWatcher
{
	public:
		static void wcharToCharArray(const WCHAR* src, char* dest, int len)
		{
			for(unsigned int i = 0; i < len / sizeof(WCHAR); ++i)
			{
				dest[i] = static_cast<char>(src[i]); 
			}
			dest[len / sizeof(WCHAR)] = '\0';
		}

		void start(LPCWSTR path)
		{
			m_overlapped.hEvent = this;
			m_handle = CreateFile(path, FILE_LIST_DIRECTORY, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
			ReadDirectoryChangesW(m_handle, m_info, sizeof(m_info), TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE, &m_received, &m_overlapped, callback);
		}

		static void CALLBACK callback(DWORD errorCode, DWORD tferred, LPOVERLAPPED over)
		{
			ASSERT(errorCode == 0);
			FileSystemWatcher* watcher = (FileSystemWatcher*)over->hEvent;
			if(tferred > 0)
			{
				switch(watcher->m_info[0].Action)
				{
					case FILE_ACTION_MODIFIED:
						{
							char tmp[MAX_PATH];
							wcharToCharArray(watcher->m_info[0].FileName, tmp, watcher->m_info[0].FileNameLength);
							watcher->m_asset_browser->emitFileChanged(tmp);
						}
						break;
				}
			}
			ReadDirectoryChangesW(watcher->m_handle, watcher->m_info, sizeof(watcher->m_info), TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE, &watcher->m_received, &watcher->m_overlapped, callback);
		}

		FILE_NOTIFY_INFORMATION m_info[10];
		HANDLE m_handle;
		DWORD m_received;
		OVERLAPPED m_overlapped;
		AssetBrowser* m_asset_browser;
};


void getDefaultFilters(QStringList& filters)
{
	filters << "*.msh" << "*.unv" << "*.ani";
}


AssetBrowser::AssetBrowser(QWidget* parent) :
	QDockWidget(parent),
	m_ui(new Ui::AssetBrowser)
{
	m_watcher = new FileSystemWatcher;
	m_watcher->start(QDir::currentPath().toStdWString().c_str());
	m_base_path = QDir::currentPath();
	m_watcher->m_asset_browser = this;
	m_client = NULL;
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


void AssetBrowser::emitFileChanged(const char* path)
{
	emit fileChanged(path);
}


void AssetBrowser::handleDoubleClick(const QFileInfo& file_info)
{
	ASSERT(m_client);
	const QString& suffix = file_info.suffix();
	QString file =file_info.filePath().toLower();
	if(suffix == "unv")
	{
		m_client->loadUniverse(file.toLatin1().data());
	}
	else if(suffix == "msh")
	{
		m_client->addEntity();
		m_client->addComponent(crc32("renderable"));
		QString base_path = m_client->getBasePath();
		if(file.startsWith(base_path))
		{
			file.remove(0, base_path.length());
		}
		m_client->setComponentProperty("renderable", "source", file.toLatin1().data(), file.length());
	}
}


void AssetBrowser::on_treeView_doubleClicked(const QModelIndex &index)
{
	ASSERT(m_model);
	handleDoubleClick(m_model->fileInfo(index));
}


void AssetBrowser::onFileChanged(const QString& path)
{
	if(m_server)
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
