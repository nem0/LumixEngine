#include "assetbrowser.h"
#include "ui_assetbrowser.h"
#include "file_system_watcher.h"
#include "core/crc32.h"
#include "core/log.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "insert_mesh_command.h"
#include "notifications.h"
#include "scripts/scriptcompiler.h"
#include <qdesktopservices.h>
#include <qfilesystemmodel.h>
#include <qinputdialog.h>
#include <qmenu.h>
#include <qmessagebox.h>
#include <qprocess.h>
#include <qurl.h>


struct ProcessInfo
{
	class QProcess* m_process;
	QString m_path;
	int m_notification_id;
};


class FlatFileListModel : public QAbstractItemModel
{
	public:
		void setFilter(const QString& filter, const QStringList& ext_filter)
		{
			m_filter.clear();
			for (auto i : ext_filter)
			{
				m_filter << QString("*") + filter + i;
			}
			m_files.clear();
			beginResetModel();
			fillList(QDir::currentPath(), m_filter);
			endResetModel();
		}


		QFileInfo fileInfo(const QModelIndex& index) const
		{
			return QFileInfo(m_files[index.row()]);
		}


		virtual QModelIndex index(int row, int column, const QModelIndex& = QModelIndex()) const override
		{
			return createIndex(row, column);
		}


		virtual QModelIndex parent(const QModelIndex&) const override
		{
			return QModelIndex();
		}


		virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override
		{
			if (parent.isValid())
				return 0;
			return m_files.size();
		}


		virtual int columnCount(const QModelIndex& = QModelIndex()) const override
		{
			return 1;
		}


		virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
		{
			if (Qt::DisplayRole == role)
			{
				return QFileInfo(m_files[index.row()]).fileName();
			}
			return QVariant();
		}


	private:
		void fillList(const QDir& dir, const QStringList& filters)
		{
			QFileInfoList list = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);

			for (int i = 0, c = list.size(); i < c; ++i)
			{
				m_files.append(list[i].filePath());
			}

			list = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);

			for (int i = 0, c = list.size(); i < c; ++i)
			{
				QString filename = list[i].fileName();
				fillList(QDir(list[i].filePath()), filters);
			}
		}

	private:
		QStringList m_filter;
		QVector<QString> m_files;
};


void getDefaultFilters(QStringList& filters)
{
	filters << "*.msh" << "*.unv" << "*.ani" << "*.blend" << "*.tga" << "*.mat" << "*.dds" << "*.fbx" << "*.shd" << "*.json";
}


AssetBrowser::AssetBrowser(QWidget* parent) :
	QDockWidget(parent),
	m_ui(new Ui::AssetBrowser)
{
	m_watcher = FileSystemWatcher::create(Lumix::Path(QDir::currentPath().toLatin1().data()));
	m_watcher->getCallback().bind<AssetBrowser, &AssetBrowser::onFileSystemWatcherCallback>(this);
	m_base_path = QDir::currentPath();
	m_editor = NULL;
	m_ui->setupUi(this);
	m_flat_filtered_model = new FlatFileListModel;
	m_model = new QFileSystemModel;
	m_model->setRootPath(QDir::currentPath());
	QStringList filters;
	getDefaultFilters(filters);
	m_model->setReadOnly(false);
	setExtentionsFilter(filters);
	m_model->setNameFilterDisables(false);
	m_ui->treeView->setModel(m_model);
	m_ui->treeView->setRootIndex(m_model->index(QDir::currentPath()));
	m_ui->treeView->hideColumn(1);
	m_ui->treeView->hideColumn(2);
	m_ui->treeView->hideColumn(3);
	m_ui->treeView->hideColumn(4);
	connect(this, &AssetBrowser::fileChanged, this, &AssetBrowser::onFileChanged);
}


AssetBrowser::~AssetBrowser()
{
	delete m_ui;
	delete m_model;
	delete m_flat_filtered_model;
}


void AssetBrowser::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
	m_editor->registerEditorCommandCreator("insert_mesh", &AssetBrowser::createInsertMeshCommand);
}


Lumix::IEditorCommand* AssetBrowser::createInsertMeshCommand(Lumix::WorldEditor& editor)
{
	return editor.getAllocator().newObject<InsertMeshCommand>(editor);
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
	QString file = file_info.filePath().toLower();
	if(suffix == "unv")
	{
		m_editor->loadUniverse(Lumix::Path(file.toLatin1().data()));
	}
	else if(suffix == "msh")
	{
		InsertMeshCommand* command = m_editor->getAllocator().newObject<InsertMeshCommand>(*m_editor, m_editor->getCameraRaycastHit(), Lumix::Path(file.toLatin1().data()));
		m_editor->executeCommand(command);
	}
	else if(suffix == "ani")
	{
		m_editor->addComponent(crc32("animable"));
		m_editor->setProperty(crc32("animable"), -1, *m_editor->getProperty("animable", "preview"), file.toLatin1().data(), file.length());
	}
	else if (suffix == "blend" || suffix == "tga" || suffix == "dds")
	{
		QDesktopServices::openUrl(QUrl::fromLocalFile(file_info.absoluteFilePath()));
	}
}


void AssetBrowser::on_treeView_doubleClicked(const QModelIndex& index)
{
	handleDoubleClick(index.model() == m_model ?  m_model->fileInfo(index) : m_flat_filtered_model->fileInfo(index));
}


void AssetBrowser::onFileChanged(const QString& path)
{
	QFileInfo info(path);
	if (info.suffix() == "cpp")
	{
		m_compiler->onScriptChanged(info.fileName().toLatin1().data());
	}
	else if(info.suffix() == "blend@")
	{
		QFileInfo file_info(path);
		QString base_name = file_info.absolutePath() + "/" + file_info.baseName() + ".blend";
		QFileInfo result_file_info(base_name);
		exportAnimation(result_file_info);
		exportModel(result_file_info);
	}
	else if(m_editor)
	{
		m_editor->getEngine().getResourceManager().reload(path.toLatin1().data());
	}
}


void AssetBrowser::on_searchInput_textEdited(const QString &arg1)
{
	if(arg1.length() == 0)
	{
		m_ui->treeView->setModel(m_model);
		m_ui->treeView->setRootIndex(m_model->index(QDir::currentPath()));
	}
	else
	{
		setExtentionsFilter(m_extension_filter);
		m_ui->treeView->setModel(m_flat_filtered_model);
	}
}


void AssetBrowser::on_exportFinished(int exit_code)
{
	QProcess* process = static_cast<QProcess*>(QObject::sender());
	QString s = process->readAll();
	process->deleteLater();
	while(process->waitForReadyRead())
	{
		s += process->readAll();
	}
	if (exit_code != 0)
	{
		auto msg = s.toLatin1();
		Lumix::g_log_error.log("editor") << msg.data();
	}

	for (auto iter = m_processes.begin(); iter != m_processes.end(); ++iter)
	{
		if (iter->m_process == process)
		{
			m_notifications->setNotificationTime(iter->m_notification_id, 1.0f);
			m_notifications->setProgress(iter->m_notification_id, 100);
			process->deleteLater();
			m_processes.erase(iter);
			break;
		}
	}
}


void AssetBrowser::exportAnimation(const QFileInfo& file_info)
{
	ProcessInfo process;
	process.m_path = file_info.path().toLatin1().data();
	process.m_process = new QProcess();
	auto message = QString("Exporting animation %1").arg(file_info.fileName()).toLatin1();
	process.m_notification_id = m_notifications->showProgressNotification(message.data());
	
	m_notifications->setProgress(process.m_notification_id, 50);
	m_processes.append(process);
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
	auto message = QString("Exporting model %1").arg(file_info.fileName()).toLatin1();
	process.m_notification_id = m_notifications->showProgressNotification(message.data());
	m_notifications->setProgress(process.m_notification_id, 50);
	m_processes.append(process);
	QStringList list;
	if (file_info.suffix() == "fbx")
	{
		list.push_back(file_info.absoluteFilePath());
		list.push_back(file_info.absolutePath() + "/" + file_info.baseName() + ".msh");
		connect(process.m_process, (void (QProcess::*)(int))&QProcess::finished, this, &AssetBrowser::on_exportFinished);
		process.m_process->start("editor/tools/fbx_converter.exe", list);
	}
	else
	{
		list.push_back("/C");
		list.push_back("models\\export_mesh.bat");
		list.push_back(file_info.absoluteFilePath().toLatin1().data());
		list.push_back(m_base_path.toLatin1().data());
		connect(process.m_process, (void (QProcess::*)(int))&QProcess::finished, this, &AssetBrowser::on_exportFinished);
		process.m_process->start("cmd.exe", list);
	}
}

void AssetBrowser::on_treeView_customContextMenuRequested(const QPoint &pos)
{
	QMenu *menu = new QMenu("Item actions",NULL);
	const QModelIndex& index = m_ui->treeView->indexAt(pos);
	const QFileInfo& file_info = index.model() == m_model ? m_model->fileInfo(index) : m_flat_filtered_model->fileInfo(index);
	QAction* selected_action = NULL;
	QAction* delete_file_action = new QAction("Delete", menu);
	menu->addAction(delete_file_action);
	QAction* rename_file_action = new QAction("Rename", menu);
	menu->addAction(rename_file_action);

	QAction* create_dir_action = new QAction("Create directory", menu);
	QAction* export_anim_action = new QAction("Export Animation", menu);
	QAction* export_model_action = new QAction("Export Model", menu);
	if (file_info.isDir())
	{
		menu->addAction(create_dir_action);
	}
	if (file_info.suffix() == "blend" || file_info.suffix() == "fbx")
	{
		menu->addAction(export_anim_action);
		menu->addAction(export_model_action);
	}
	selected_action = menu->exec(mapToGlobal(pos));
	if (selected_action == export_anim_action)
	{
		exportAnimation(file_info);
	}
	else if (selected_action == export_model_action)
	{
		exportModel(file_info);
	}
	else if (selected_action == delete_file_action)
	{
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, "Delete", "Are you sure?", QMessageBox::Yes | QMessageBox::No);
		if (reply == QMessageBox::Yes)
		{
			if (file_info.isFile())
			{
				QFile::remove(file_info.absoluteFilePath());
			}
			else
			{
				QDir dir(file_info.absoluteFilePath());
				dir.removeRecursively();
			}
		}
	}
	else if (selected_action == rename_file_action)
	{
		m_ui->treeView->edit(index);
	}
	else if (selected_action == create_dir_action)
	{
		bool ok;
		QString text = QInputDialog::getText(this, "Create directory", "Directory name:", QLineEdit::Normal, QDir::home().dirName(), &ok);
		if (ok && !text.isEmpty())
		{
			QDir().mkdir(file_info.absoluteFilePath() + "/" + text);
		}
	}
}

void AssetBrowser::setExtentionsFilter(const QStringList& filters)
{
	m_flat_filtered_model->setFilter(m_ui->searchInput->text(), filters);
	m_model->setNameFilters(filters);
	m_extension_filter = filters;
}


void AssetBrowser::on_filterComboBox_currentTextChanged(const QString&)
{
	QStringList filters;
	if(m_ui->filterComboBox->currentText() == "All")
	{
		getDefaultFilters(filters);
	}
	else if (m_ui->filterComboBox->currentText() == "Animation")
	{
		filters << "*.ani";
	}
	else if (m_ui->filterComboBox->currentText() == "Mesh")
	{
		filters << "*.msh";
	}
	else if (m_ui->filterComboBox->currentText() == "Material")
	{
		filters << "*.mat";
	}
	else if (m_ui->filterComboBox->currentText() == "Pipeline")
	{
		filters << "*.json";
	}
	else if (m_ui->filterComboBox->currentText() == "Shader")
	{
		filters << "*.shd";
	}
	else if (m_ui->filterComboBox->currentText() == "Texture")
	{
		filters << "*.tga" << "*.dds";
	}
	setExtentionsFilter(filters);
}


void AssetBrowser::on_treeView_clicked(const QModelIndex &index)
{
	if (index.isValid())
	{
		if (index.model() == m_model)
		{
			const QFileInfo& file_info = m_model->fileInfo(index);
			if (file_info.isFile())
			{
				QByteArray byte_array = file_info.filePath().toLower().toLatin1();
				const char* filename = byte_array.data();
				emit fileSelected(filename);
			}
		}
		else
		{
			const QFileInfo& file_info = m_flat_filtered_model->fileInfo(index);
			if (file_info.isFile())
			{
				QByteArray byte_array = file_info.filePath().toLower().toLatin1();
				const char* filename = byte_array.data();
				emit fileSelected(filename);
			}
		}
	}
}
