#pragma once

#include <QDockWidget>

namespace Ui 
{
	class MaterialManager;
}

namespace Lux
{
	class EditorServer;
	class EditorClient;
	class Event;
}

class MaterialManager : public QDockWidget
{
		Q_OBJECT

	public:
		explicit MaterialManager(QWidget* parent = NULL);
		~MaterialManager();
		void setEditorServer(Lux::EditorServer& server);
		void setEditorClient(Lux::EditorClient& client);
		void updatePreview();

	private:
		void onPropertyList(Lux::Event& event);
		void fillObjectMaterials();
		void selectMaterial(const char* path);

	private slots:
		void on_fileListView_doubleClicked(const QModelIndex& index);
		void on_objectMaterialList_doubleClicked(const QModelIndex& index);
		void onBoolPropertyStateChanged(int state);
		void onShaderChanged();
		void onTextureChanged();

	private:
		Ui::MaterialManager* m_ui;
		class MaterialManagerUI* m_impl;
};

