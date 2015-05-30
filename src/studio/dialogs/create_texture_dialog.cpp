#include "create_texture_dialog.h"
#include "ui_create_texture_dialog.h"
#include <qfile.h>
#include <qmessagebox.h>


CreateTextureDialog::CreateTextureDialog(QWidget* parent, const QString& dir)
	: QDialog(parent)
	, m_dir(dir)
{
	m_ui = new Ui::CreateTextureDialog;
	m_ui->setupUi(this);
	connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &CreateTextureDialog::onAccepted);
}


CreateTextureDialog::~CreateTextureDialog()
{
	delete m_ui;
}


void CreateTextureDialog::onAccepted()
{
	auto filepath = m_dir + m_ui->nameInput->text();
	if (QFile::exists(filepath))
	{
		if (QMessageBox::No == QMessageBox::question(this, "Overwrite", "File already exists, overwrite?"))
		{
			return;
		}
	}
	QFile file(filepath);
	if (file.open(QIODevice::WriteOnly))
	{
		quint16 value = 0;
		int size = m_ui->sizeInput->value();
		for (int i = 0; i < size * size; ++i)
		{
			file.write((const char*)&value, sizeof(value));
		}
		file.close();
	}
}

