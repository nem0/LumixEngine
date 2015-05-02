#include "import_asset_dialog.h"
#include "ui_import_asset_dialog.h"
#include "obj_file.h"
#include <qfile.h>
#include <qfiledialog.h>
#include <qmessagebox.h>


ImportAssetDialog::ImportAssetDialog(QWidget* parent)
	: QDialog(parent)
{
	m_ui = new Ui::ImportAssetDialog;
	m_ui->setupUi(this);
	connect(m_ui->sourceInput, &QLineEdit::textChanged, [this](const QString&) { updateStatus(); });
	connect(m_ui->destinationInput, &QLineEdit::textChanged, [this](const QString&) { updateStatus(); });
	m_ui->destinationInput->setText(QDir::currentPath());
	updateStatus();
}


void ImportAssetDialog::on_browseSourceButton_clicked()
{
	QString path = QFileDialog::getOpenFileName(this, "Select source", QString(), "Model files (*.obj)");
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


void ImportAssetDialog::setModelInput(const QString& source, const QString& destination)
{
	m_ui->tabWidget->setCurrentIndex(0);
	m_ui->sourceInput->setText(source);
	m_ui->destinationInput->setText(destination);
}


void ImportAssetDialog::on_importButton_clicked()
{
	Q_ASSERT(!m_ui->sourceInput->text().isEmpty());
	Q_ASSERT(!m_ui->destinationInput->text().isEmpty());

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
				|| file.saveLumixMaterials(m_ui->destinationInput->text() + "/" + source_info.baseName() + ".msh");
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


void ImportAssetDialog::updateStatus()
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


ImportAssetDialog::~ImportAssetDialog()
{
	delete m_ui;
}