#include "import_asset_dialog.h"
#include "ui_import_asset_dialog.h"
#include "obj_file.h"
#include "core/log.h"
#include <qfile.h>
#include <qfiledialog.h>
#include <qimagewriter.h>
#include <qmessagebox.h>
#include <qprocess.h>


ImportAssetDialog::ImportAssetDialog(QWidget* parent, const QString& base_path)
	: QDialog(parent)
	, m_base_path(base_path)
{
	m_ui = new Ui::ImportAssetDialog;
	m_ui->setupUi(this);
	connect(m_ui->sourceInput, &QLineEdit::textChanged, [this](const QString&) { updateStatus(); });
	connect(m_ui->destinationInput, &QLineEdit::textChanged, [this](const QString&) { updateStatus(); });
	connect(m_ui->animationSourceInput, &QLineEdit::textChanged, [this](const QString&) { updateStatus(); });
	connect(m_ui->textureSourceInput, &QLineEdit::textChanged, [this](const QString&) { updateStatus(); });
	m_ui->destinationInput->setText(QDir::currentPath());
	updateStatus();
}


void ImportAssetDialog::on_browseTextureButton_clicked()
{
	QString path = QFileDialog::getOpenFileName(this, "Select source", QString(), "Texture files (*.png)");
	if (!path.isEmpty())
	{
		m_ui->textureSourceInput->setText(path);
	}
}


void ImportAssetDialog::on_browseAnimationSourceButton_clicked()
{
	QString path = QFileDialog::getOpenFileName(this, "Select source", QString(), "Animation files (*.blend)");
	if (!path.isEmpty())
	{
		m_ui->animationSourceInput->setText(path);
	}
}


void ImportAssetDialog::on_browseSourceButton_clicked()
{
	QString path = QFileDialog::getOpenFileName(this, "Select source", QString(), "Model files (*.obj, *.blend)");
	if (!path.isEmpty())
	{
		m_ui->sourceInput->setText(path);
	}
}


void ImportAssetDialog::on_browseDestinationButton_clicked()
{
	QString path = QFileDialog::getExistingDirectory(this, "Select destination", QDir::currentPath());
	if (!path.isEmpty())
	{
		m_ui->destinationInput->setText(path);
	}
}


bool ImportAssetDialog::createMaterials(OBJFile& file, const QString& path)
{
	if (m_ui->materialCombobox->currentText() != "Create materials")
	{
		return true;
	}
	m_ui->progressBar->setValue(95);
	m_ui->statusLabel->setText("Creating materials");
	for (int i = 0; i < file.getMeshCount(); ++i)
	{
		QFile output(path + "/" + file.getMaterialName(i) + ".mat");
		if (!output.open(QIODevice::WriteOnly))
		{
			return false;
		}
		output.write("{	\"texture\" : { \"source\" : \"texture.dds\" }, \"shader\" : \"shaders/rigid.shd\" }");
		output.close();
	}
	return true;
}


void ImportAssetDialog::setDestination(const QString& destination)
{
	m_ui->destinationInput->setText(destination);
}


void ImportAssetDialog::setAnimationSource(const QString& source)
{
	m_ui->tabWidget->setCurrentIndex(1);
	m_ui->animationSourceInput->setText(source);
}


void ImportAssetDialog::setModelSource(const QString& source)
{
	m_ui->tabWidget->setCurrentIndex(0);
	m_ui->sourceInput->setText(source);
}


void ImportAssetDialog::importOBJ()
{
	m_ui->progressBar->setValue(75);
	m_ui->statusLabel->setText("Importing...");
	if (!QFileInfo::exists(m_ui->destinationInput->text()))
	{
		if (QMessageBox::question(this, "Create destination directory", "Destination directory does not exist. Create it?") == QMessageBox::StandardButton::No)
		{
			m_ui->progressBar->setValue(100);
			m_ui->statusLabel->setText("Destination directory does not exist.");
			return;
		}
		else
		{
			QDir dir(m_ui->destinationInput->text());
			if (!dir.mkpath("."))
			{
				m_ui->progressBar->setValue(100);
				m_ui->statusLabel->setText("Destination directory could not be created.");
				return;
			}
		}
	}

	OBJFile file;
	if (file.load(m_ui->sourceInput->text()))
	{
		if (!m_ui->destinationInput->text().isEmpty())
		{
			QFileInfo source_info(m_ui->sourceInput->text());
			bool save_mesh_success = file.saveLumixMesh(m_ui->destinationInput->text() + "/" + source_info.baseName() + ".msh");
			bool save_materials_success = m_ui->materialCombobox->currentText() != "Import materials"
				|| file.saveLumixMaterials(m_ui->destinationInput->text() + "/" + source_info.baseName() + ".msh", m_ui->convertToDDSCheckbox->isChecked());
			bool create_materials_success = createMaterials(file, m_ui->destinationInput->text());
			if (save_mesh_success && save_materials_success && create_materials_success)
			{
				m_ui->progressBar->setValue(100);
				m_ui->statusLabel->setText("Import successful");
				return;
			}
		}
	}
	m_ui->progressBar->setValue(100);
	m_ui->statusLabel->setText("Import failed");
}


void ImportAssetDialog::importAnimation()
{
	Q_ASSERT(!m_ui->animationSourceInput->text().isEmpty());

	m_ui->progressBar->setValue(75);
	m_ui->statusLabel->setText("Importing...");
	QFileInfo file_info(m_ui->animationSourceInput->text());
	QProcess* process = new QProcess(this);
	QStringList list;
	list.push_back("/C");
	list.push_back("models\\export_anim.bat");
	list.push_back(file_info.absoluteFilePath());
	list.push_back(m_ui->destinationInput->text() + "/" + file_info.baseName() + ".ani");
	list.push_back(m_base_path);
	connect(process, (void (QProcess::*)(int))&QProcess::finished, [process, this](int exit_code) {
		QString s = process->readAll();
		process->deleteLater();
		while (process->waitForReadyRead())
		{
			s += process->readAll();
		}
		process->deleteLater();
		if (exit_code != 0)
		{
			Lumix::g_log_error.log("import") << s.toLatin1().data();
			m_ui->progressBar->setValue(100);
			m_ui->statusLabel->setText("Import failed.");
			return;
		}
		m_ui->progressBar->setValue(100);
		m_ui->statusLabel->setText("Import successful.");
	});
	process->start("cmd.exe", list);
}


void ImportAssetDialog::importBlender()
{
	m_ui->progressBar->setValue(75);
	m_ui->statusLabel->setText("Importing...");
	QFileInfo file_info(m_ui->sourceInput->text());
	QProcess* process = new QProcess(this);
	QStringList list;
	list.push_back("/C");
	list.push_back("models\\export_mesh.bat");
	list.push_back(file_info.absoluteFilePath());
	list.push_back(m_ui->destinationInput->text() + "/" + file_info.baseName() + ".msh");
	list.push_back(m_base_path);
	connect(process, (void (QProcess::*)(int))&QProcess::finished, [process, this](int exit_code) {
		QString s = process->readAll();
		process->deleteLater();
		while (process->waitForReadyRead())
		{
			s += process->readAll();
		}
		process->deleteLater();
		if (exit_code != 0)
		{
			Lumix::g_log_error.log("import") << s.toLatin1().data();
			m_ui->progressBar->setValue(100);
			m_ui->statusLabel->setText("Import failed.");
			return;
		}
		m_ui->progressBar->setValue(100);
		m_ui->statusLabel->setText("Import successful.");
	});
	process->start("cmd.exe", list);
}


void ImportAssetDialog::importTexture()
{
	Q_ASSERT(!m_ui->textureSourceInput->text().isEmpty());

	m_ui->progressBar->setValue(75);
	m_ui->statusLabel->setText("Importing...");
	QFileInfo source_info(m_ui->textureSourceInput->text());
	QImage img(source_info.absoluteFilePath());
	QImageWriter writer(m_ui->destinationInput->text() + "/" + source_info.baseName() + ".dds");
	if (!writer.write(img.mirrored()))
	{
		m_ui->statusLabel->setText("Import failed.");
	}
	else
	{
		m_ui->statusLabel->setText("Import successful.");
	}
	m_ui->progressBar->setValue(100);
}


void ImportAssetDialog::on_importButton_clicked()
{
	Q_ASSERT(!m_ui->destinationInput->text().isEmpty());

	QFileInfo source_info(m_ui->sourceInput->text());

	if (m_ui->tabWidget->currentIndex() == 0)
	{
		Q_ASSERT(!m_ui->sourceInput->text().isEmpty());
		if (source_info.suffix() == "obj")
		{
			importOBJ();
			return;
		}
		else if (source_info.suffix() == "blend")
		{
			importBlender();
			return;
		}
	}
	else if (m_ui->tabWidget->currentIndex() == 1)
	{
		importAnimation();
		return;
	}
	else
	{
		importTexture();
		return;
	}
	Q_ASSERT(false);
	m_ui->progressBar->setValue(100);
	m_ui->statusLabel->setText("Error.");
}


void ImportAssetDialog::updateStatus()
{
	if (m_ui->tabWidget->currentIndex() == 0)
	{
		if (m_ui->sourceInput->text().isEmpty())
		{
			m_ui->progressBar->setValue(1);
			m_ui->statusLabel->setText("Source empty");
			m_ui->importButton->setEnabled(false);
		}
		else if (m_ui->destinationInput->text().isEmpty())
		{
			m_ui->progressBar->setValue(25);
			m_ui->statusLabel->setText("Destination empty");
			m_ui->importButton->setEnabled(false);
		}
		else
		{
			m_ui->progressBar->setValue(50);
			m_ui->statusLabel->setText("Import possible");
			m_ui->importButton->setEnabled(true);
		}
	}

	if (m_ui->tabWidget->currentIndex() == 1)
	{
		bool is_blender = m_ui->animationSourceInput->text().endsWith(".blend");
		if (is_blender)
		{
			m_ui->statusLabel->setText("Import possible");
		}
		else
		{
			m_ui->statusLabel->setText("Unsupported file type");
		}
		m_ui->importButton->setEnabled(is_blender);
	}

	if (m_ui->tabWidget->currentIndex() == 2)
	{
		bool is_importable = m_ui->textureSourceInput->text().endsWith(".png");
		if (is_importable)
		{
			m_ui->statusLabel->setText("Import possible");
		}
		else
		{
			m_ui->statusLabel->setText("Unsupported file type");
		}
		m_ui->importButton->setEnabled(is_importable);
	}

	bool is_blender = m_ui->sourceInput->text().endsWith(".blend");
	m_ui->materialCombobox->setEnabled(!is_blender);
	m_ui->convertToDDSCheckbox->setEnabled(!is_blender);
}


ImportAssetDialog::~ImportAssetDialog()
{
	delete m_ui;
}