#pragma once


#include "assimp/Importer.hpp"
#include "assimp/progresshandler.hpp"
#include <qdialog.h>
#include <qthread.h>


namespace Ui
{
	class ImportAssetDialog;
}


class ImportAssetDialog;
class QFile;


class ImportThread : public QThread, public Assimp::ProgressHandler
{
	Q_OBJECT

	public:
		enum Status
		{
			EMPTY,
			LOADING_SOURCE,
			SOURCE_LOADED,
			SAVING,
			SAVED,
			FAIL
		};

	public:
		ImportThread(ImportAssetDialog& dialog);
		virtual ~ImportThread();
	
		virtual bool Update(float percentage = -1.f) override { emit progress(percentage / 3); return true; }
		virtual void run() override;
		void setSource(const QString& source) { m_source = source; }
		void setDestination(const QString& destination) { m_destination = destination; }
		void setConvertTexturesToDDS(bool convert) { m_convert_texture_to_DDS = convert; }
		void setImportMaterials(bool import_materials) { m_import_materials = import_materials; }
		Status getStatus() const { return m_status; }

	private:
		void writeSkeleton(QFile& file);
		void writeMeshes(QFile& file);
		void writeGeometry(QFile& file);
		bool saveLumixMaterials();
		bool saveLumixMesh();

	signals:
		void message(QString message);
		void progress(float percentage);

	private:
		QString m_source;
		QString m_destination;
		ImportAssetDialog& m_dialog;
		bool m_import_materials;
		bool m_convert_texture_to_DDS;
		Assimp::Importer& m_importer;
		class LogStream* m_log_stream;
		Status m_status;
};


class ImportAssetDialog : public QDialog
{
	Q_OBJECT

	public:
		ImportAssetDialog(QWidget* parent, const QString& base_path);
		virtual ~ImportAssetDialog();

		void setSource(const QString& source);
		void setDestination(const QString& destination);
		Assimp::Importer& getImporter() { return m_importer; }

	private:
		void importModel();
		void importAnimation();
		void importTexture();
		bool saveLumixMesh(const QString& path);
		bool saveLumixMaterials(const QString& path);
		void writeMeshes(QFile& file);
		void writeGeometry(QFile& file);

	private slots:
		void on_browseSourceButton_clicked();
		void on_browseDestinationButton_clicked();
		void on_importButton_clicked();
		void on_progressUpdate(float percentage);
		void on_importFinished();
		void on_sourceInput_textChanged(const QString& text);
		void on_importMaterialsCheckbox_stateChanged(int);
		void on_importMessage(QString message);

	private:
		Ui::ImportAssetDialog* m_ui;
		QString m_base_path;
		ImportThread* m_import_thread;
		Assimp::Importer m_importer;
};