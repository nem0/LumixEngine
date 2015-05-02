#pragma once


#include<qdialog.h>


namespace Ui
{
	class ImportAssetDialog;
}


class ImportAssetDialog : public QDialog
{
	Q_OBJECT

	public:
		explicit ImportAssetDialog(QWidget* parent);
		void setModelInput(const QString& source, const QString& destination);
		~ImportAssetDialog();

	private:
		void updateStatus();
		bool createMaterials(class OBJFile& file, const QString& path);

	private slots:
		void on_browseSourceButton_clicked();
		void on_browseDestinationButton_clicked();
		void on_importButton_clicked();

	private:
		Ui::ImportAssetDialog* m_ui;
};