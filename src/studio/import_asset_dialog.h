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
		ImportAssetDialog(QWidget* parent, const QString& base_path);
		void setModelSource(const QString& source);
		void setDestination(const QString& destination);
		void setAnimationSource(const QString& source);
		~ImportAssetDialog();

	private:
		void updateStatus();
		bool createMaterials(class OBJFile& file, const QString& path);
		void importOBJ();
		void importBlender();
		void importAnimation();
		void importTexture();

	private slots:
		void on_browseTextureButton_clicked();
		void on_browseAnimationSourceButton_clicked();
		void on_browseSourceButton_clicked();
		void on_browseDestinationButton_clicked();
		void on_importButton_clicked();

	private:
		Ui::ImportAssetDialog* m_ui;
		QString m_base_path;
};