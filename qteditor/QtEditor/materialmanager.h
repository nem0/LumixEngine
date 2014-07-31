#pragma once

#include <QDockWidget>
#include "core/resource.h"

namespace Ui 
{
	class MaterialManager;
}

namespace Lumix
{
	class EditorServer;
	class Event;
	struct PropertyListEvent;
}

class MaterialManager : public QDockWidget
{
		Q_OBJECT

	public:
		explicit MaterialManager(QWidget* parent = NULL);
		~MaterialManager();
		void setEditorServer(Lumix::EditorServer& server);
		void updatePreview();

	private:
		void onPropertyList(Lumix::PropertyListEvent& event);
		void fillObjectMaterials();
		void selectMaterial(const char* path);
		void onMaterialLoaded(Lumix::Resource::State, Lumix::Resource::State);

	private slots:
		void on_fileListView_doubleClicked(const QModelIndex& index);
		void on_objectMaterialList_doubleClicked(const QModelIndex& index);
		void on_saveMaterialButton_clicked();
		void onBoolPropertyStateChanged(int state);
		void onShaderChanged();
		void onTextureChanged();
		void onTextureRemoved();
		void onTextureAdded();

private:
		Ui::MaterialManager* m_ui;
		class MaterialManagerUI* m_impl;
};

