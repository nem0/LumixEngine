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
		~ImportAssetDialog();

	private:
		void updateStatus();

	private slots:
		void on_browseSourceButton_clicked();
		void on_browseDestinationButton_clicked();
		void on_importButton_clicked();

	private:
		Ui::ImportAssetDialog* m_ui;
};