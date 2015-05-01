#include "import_asset_dialog.h"
#include "ui_import_asset_dialog.h"
#include "obj_file.h"
#include <qfiledialog.h>


ImportAssetDialog::ImportAssetDialog(QWidget* parent)
	: QDialog(parent)
{
	m_ui = new Ui::ImportAssetDialog;
	m_ui->setupUi(this);
	connect(m_ui->sourceInput, &QLineEdit::textChanged, [this](const QString&) { updateStatus(); });
	connect(m_ui->destinationInput, &QLineEdit::textChanged, [this](const QString&) { updateStatus(); });
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


void ImportAssetDialog::on_importButton_clicked()
{
	Q_ASSERT(!m_ui->sourceInput->text().isEmpty());
	Q_ASSERT(!m_ui->destinationInput->text().isEmpty());

	m_ui->progressBar->setValue(75);
	m_ui->statusLabel->setText("Importing...");
	OBJFile file;
	if (file.load(m_ui->sourceInput->text()))
	{
		if (!m_ui->destinationInput->text().isEmpty())
		{
			QFileInfo source_info(m_ui->sourceInput->text());
			if (file.saveLumixMesh(m_ui->destinationInput->text() + "/" + source_info.baseName() + ".msh"));
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