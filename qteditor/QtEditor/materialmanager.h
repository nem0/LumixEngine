#pragma once

#include <QDockWidget>
#include "core/resource.h"

namespace Ui 
{
	class MaterialManager;
}

namespace Lumix
{
	class WorldEditor;
	struct Entity;
	class Event;
}

class MaterialManager : public QDockWidget
{
		Q_OBJECT

	public:
		explicit MaterialManager(QWidget* parent = NULL);
		~MaterialManager();
		void setWorldEditor(Lumix::WorldEditor& editor);
		void updatePreview();
		QWidget* getPreview() const;

	private:
		void fillObjectMaterials();
		void selectMaterial(const char* path);
		void onMaterialLoaded(Lumix::Resource::State, Lumix::Resource::State);
		void onEntitySelected(Lumix::Entity& entity);

	private slots:
		void on_objectMaterialList_doubleClicked(const QModelIndex& index);
		void on_saveMaterialButton_clicked();
		void onBoolPropertyStateChanged(int state);
		void onShaderChanged();
		void onTextureChanged();
		void onTextureRemoved();
		void onTextureAdded();
        void on_fileTreeView_doubleClicked(const QModelIndex &index);

private:
		Ui::MaterialManager* m_ui;
		class MaterialManagerUI* m_impl;
};

