#include "assetbrowser.h"
#include "ui_assetbrowser.h"
#include "assimp/Importer.hpp"
#include "file_system_watcher.h"
#include "core/crc32.h"
#include "core/log.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "dialogs/create_texture_dialog.h"
#include "dialogs/import_asset_dialog.h"
#include "insert_mesh_command.h"
#include "mainwindow.h"
#include "metadata.h"
#include "notifications.h"
#include "shader_compiler.h"
#include <qdesktopservices.h>
#include <qfileiconprovider.h>
#include <qfilesystemmodel.h>
#include <qimagereader.h>
#include <qinputdialog.h>
#include <qmenu.h>
#include <qmessagebox.h>
#include <qmimedata.h>
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


	const QFileInfo& fileInfo(const QModelIndex& index) const
	{
		return m_files[index.row()];
	}


	QStringList mimeTypes() const override
	{
		QStringList types;
		types << "text/uri-list";
		return types;
	}


	virtual Qt::ItemFlags flags(const QModelIndex& index) const override
	{
		Qt::ItemFlags default_flags = QAbstractItemModel::flags(index);
		if (index.isValid())
			return Qt::ItemIsDragEnabled | default_flags;
		else
			return default_flags;
	}


	QMimeData* mimeData(const QModelIndexList& indexes) const override
	{
		QMimeData* mime_data = new QMimeData();
		QList<QUrl> urls;
		for (auto& index : indexes)
		{
			urls.push_back(
				QUrl::fromLocalFile(m_files[index.row()].filePath()));
		}
		mime_data->setUrls(urls);
		return mime_data;
	}


	virtual Qt::DropActions supportedDragActions() const override
	{
		return Qt::CopyAction | Qt::MoveAction;
	}


	virtual QModelIndex index(int row,
							  int column,
							  const QModelIndex& = QModelIndex()) const override
	{
		return createIndex(row, column);
	}


	virtual QModelIndex parent(const QModelIndex&) const override
	{
		return QModelIndex();
	}


	virtual int
	rowCount(const QModelIndex& parent = QModelIndex()) const override
	{
		if (parent.isValid())
			return 0;
		return m_files.size();
	}


	virtual int columnCount(const QModelIndex& = QModelIndex()) const override
	{
		return 1;
	}


	virtual QVariant data(const QModelIndex& index,
						  int role = Qt::DisplayRole) const override
	{
		if (Qt::DecorationRole == role && index.column() == 0)
		{
			return m_icons[index.row()];
		}
		if (Qt::DisplayRole == role)
		{
			return m_files[index.row()].fileName();
		}
		return QVariant();
	}


private:
	void fillList(const QDir& dir, const QStringList& filters)
	{
		QFileInfoList list = dir.entryInfoList(
			filters, QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);

		for (int i = 0, c = list.size(); i < c; ++i)
		{
			m_icons.append(m_icon_provider.icon(list[i].filePath()));
			m_files.append(list[i].filePath());
		}

		list =
			dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);

		for (int i = 0, c = list.size(); i < c; ++i)
		{
			QString filename = list[i].fileName();
			fillList(QDir(list[i].filePath()), filters);
		}
	}

private:
	QFileIconProvider m_icon_provider;
	QStringList m_filter;
	QVector<QFileInfo> m_files;
	QVector<QIcon> m_icons;
};


static void getTextureFilters(QStringList& filters, bool prepend_asterisk)
{
	auto image_formats = QImageReader::supportedImageFormats();
	if (prepend_asterisk)
	{
		for (auto format : image_formats)
		{
			filters << QString("*.") + format;
		}
	}
	else
	{
		for (auto format : image_formats)
		{
			filters << format;
		}
	}
}


static bool isAssimpAsset(const QString& suffix)
{
	Assimp::Importer importer;
	aiString extension_list;
	importer.GetExtensionList(extension_list);
	return QString(extension_list.C_Str())
		.split(';')
		.contains(QString("*.") + suffix);
}


static void getDefaultFilters(QStringList& filters)
{
	filters << "*.msh"
			<< "*.unv"
			<< "*.ani"
			<< "*.mat"
			<< "*.fbx"
			<< "*.shd"
			<< "*.json"
			<< "*.phy"
			<< "*.lua";
	Assimp::Importer importer;
	aiString extension_list;
	importer.GetExtensionList(extension_list);
	filters << QString(extension_list.C_Str()).split(';');
	getTextureFilters(filters, true);
}


AssetBrowser::AssetBrowser(MainWindow& main_window, QWidget* parent)
	: QDockWidget(parent)
	, m_ui(new Ui::AssetBrowser)
	, m_main_window(main_window)
{
	m_watcher = FileSystemWatcher::create(QDir::currentPath());
	m_watcher->getCallback()
		.bind<AssetBrowser, &AssetBrowser::onFileSystemWatcherCallback>(this);
	m_base_path = QDir::currentPath();
	m_editor = nullptr;
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
	connect(
		this, &AssetBrowser::fileChanged, this, &AssetBrowser::onFileChanged);
}


AssetBrowser::~AssetBrowser()
{
	FileSystemWatcher::destroy(m_watcher);
	delete m_ui;
	delete m_model;
	delete m_flat_filtered_model;
}


void AssetBrowser::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
	m_editor->registerEditorCommandCreator(
		"insert_mesh", &AssetBrowser::createInsertMeshCommand);
}


Lumix::IEditorCommand*
AssetBrowser::createInsertMeshCommand(Lumix::WorldEditor& editor)
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
	QStringList texture_filters;
	getTextureFilters(texture_filters, false);

	if (suffix == "unv")
	{
		m_editor->loadUniverse(Lumix::Path(file.toLatin1().data()));
	}
	else if (suffix == "msh")
	{
		InsertMeshCommand* command =
			m_editor->getAllocator().newObject<InsertMeshCommand>(
				*m_editor,
				m_editor->getCameraRaycastHit(),
				Lumix::Path(file.toLatin1().data()));
		m_editor->executeCommand(command);
	}
	else if (suffix == "ani")
	{
		m_editor->addComponent(Lumix::crc32("animable"));
		m_editor->setProperty(Lumix::crc32("animable"),
							  -1,
							  *m_editor->getProperty("animable", "preview"),
							  file.toLatin1().data(),
							  file.length());
	}
	else if (isAssimpAsset(suffix) || texture_filters.contains(suffix) ||
			 suffix == "shd" || suffix == "lua")
	{
		QDesktopServices::openUrl(
			QUrl::fromLocalFile(file_info.absoluteFilePath()));
	}
}


void AssetBrowser::on_treeView_doubleClicked(const QModelIndex& index)
{
	handleDoubleClick(index.model() == m_model
						  ? m_model->fileInfo(index)
						  : m_flat_filtered_model->fileInfo(index));
}


void AssetBrowser::onFileChanged(const QString& path)
{
	QFileInfo info(path);
	if (info.suffix() == "blend@")
	{
		QFileInfo file_info(path);
		QString base_name =
			file_info.absolutePath() + "/" + file_info.baseName() + ".blend";
		QFileInfo result_file_info(base_name);
		importAsset(result_file_info);
	}
	else if (m_editor)
	{
		m_editor->getEngine().getResourceManager().reload(
			path.toLatin1().data());
	}
}


void AssetBrowser::on_searchInput_textEdited(const QString& arg1)
{
	if (arg1.length() == 0)
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
	while (process->waitForReadyRead())
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


void AssetBrowser::importAsset(const QFileInfo& file_info)
{
	ImportAssetDialog* dlg =
		new ImportAssetDialog(m_main_window, this, m_base_path);
	if (!file_info.isDir())
	{
		dlg->setSource(file_info.filePath());
		dlg->setDestination(file_info.dir().path());
	}
	else
	{
		dlg->setDestination(file_info.absoluteFilePath());
	}
	dlg->show();
}


void AssetBrowser::reimportAsset(const QString& filepath)
{
	QString import_source =
		m_main_window.getMetadata()->get(filepath, "import_source").toString();

	ImportAssetDialog* dlg =
		new ImportAssetDialog(m_main_window, this, m_base_path);
	dlg->setSource(import_source);
	QFileInfo destination_info(filepath);
	dlg->setCreateDirectory(false);
	dlg->setDestination(destination_info.absolutePath());
	dlg->show();
}


void AssetBrowser::on_treeView_customContextMenuRequested(const QPoint& pos)
{
	QMenu* menu = new QMenu("Item actions", nullptr);
	const QModelIndex& index = m_ui->treeView->indexAt(pos);
	QFileInfo root_info(QDir::currentPath());
	const QFileInfo& file_info =
		index.isValid()
			? index.model() == m_model ? m_model->fileInfo(index)
									   : m_flat_filtered_model->fileInfo(index)
			: root_info;
	QAction* selected_action = nullptr;
	QAction* delete_file_action = new QAction("Delete", menu);
	QAction* rename_file_action = new QAction("Rename", menu);
	QAction* create_dir_action = new QAction("Create directory", menu);
	QAction* create_material_action = new QAction("Create material", menu);
	QAction* create_raw_texture_action =
		new QAction("Create raw texture", menu);
	QAction* import_asset_action = new QAction("Import asset", menu);
	QAction* reimport_asset_action = new QAction("Reimport asset", menu);

	menu->addAction(delete_file_action);
	menu->addAction(rename_file_action);
	if (file_info.isDir())
	{
		menu->addAction(import_asset_action);
		menu->addAction(create_dir_action);
		menu->addAction(create_material_action);
		menu->addAction(create_raw_texture_action);
	}

	char relative_path[Lumix::MAX_PATH_LENGTH];
	m_editor->getRelativePath(
		relative_path,
		sizeof(relative_path),
		Lumix::Path(file_info.absoluteFilePath().toLatin1().data()));
	if (m_main_window.getMetadata()->exists(relative_path, "import_source"))
	{
		menu->addAction(reimport_asset_action);
	}

	QStringList texture_filters;
	getTextureFilters(texture_filters, false);
	if (isAssimpAsset(file_info.suffix()) ||
		texture_filters.contains(file_info.suffix()))
	{
		menu->addAction(import_asset_action);
	}
	selected_action = menu->exec(mapToGlobal(pos));
	if (selected_action == import_asset_action)
	{
		importAsset(file_info);
	}
	else if (selected_action == reimport_asset_action)
	{
		reimportAsset(relative_path);
	}
	else if (selected_action == delete_file_action)
	{
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this,
									  "Delete",
									  "Are you sure?",
									  QMessageBox::Yes | QMessageBox::No);
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
		QString text = QInputDialog::getText(this,
											 "Create directory",
											 "Directory name:",
											 QLineEdit::Normal,
											 QDir::home().dirName(),
											 &ok);
		if (ok && !text.isEmpty())
		{
			QDir().mkdir(file_info.absoluteFilePath() + "/" + text);
		}
	}
	else if (selected_action == create_material_action)
	{
		auto material_name = QInputDialog::getText(
			nullptr, "Set filename", "Filename", QLineEdit::Normal, ".mat");
		auto path = file_info.absoluteFilePath() + "/" + material_name;
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly))
		{
			QMessageBox::warning(nullptr,
								 "Error",
								 QString("Could not create file %1").arg(path),
								 QMessageBox::StandardButton::Ok);
		}
		else
		{
			file.close();
		}
	}
	else if (selected_action == create_raw_texture_action)
	{
		createRawTexture(file_info.absoluteFilePath() + "/");
	}
}

void AssetBrowser::createRawTexture(const QString& path)
{
	auto dlg = new CreateTextureDialog(this, path);
	dlg->exec();
	dlg->deleteLater();
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
	if (m_ui->filterComboBox->currentText() == "All")
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
	else if (m_ui->filterComboBox->currentText() == "Physics")
	{
		filters << "*.phy";
	}
	else if (m_ui->filterComboBox->currentText() == "Shader")
	{
		filters << "*.shd";
	}
	else if (m_ui->filterComboBox->currentText() == "Texture")
	{
		getTextureFilters(filters, true);
	}
	setExtentionsFilter(filters);
}


void AssetBrowser::on_treeView_clicked(const QModelIndex& index)
{
	if (index.isValid())
	{
		if (index.model() == m_model)
		{
			const QFileInfo& file_info = m_model->fileInfo(index);
			if (file_info.isFile())
			{
				QByteArray byte_array =
					file_info.filePath().toLower().toLatin1();
				const char* filename = byte_array.data();
				emit fileSelected(filename);
			}
		}
		else
		{
			const QFileInfo& file_info = m_flat_filtered_model->fileInfo(index);
			if (file_info.isFile())
			{
				QByteArray byte_array =
					file_info.filePath().toLower().toLatin1();
				const char* filename = byte_array.data();
				emit fileSelected(filename);
			}
		}
	}
}
