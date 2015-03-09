#pragma once


#include "core/array.h"
#include "graphics/model.h"
#include "universe/entity.h"
#include <qdockwidget.h>
#include <qtreewidget.h>


namespace Lumix
{
	class WorldEditor;
}


class QTreeWidgetItem;


class SkeletonView : public QDockWidget
{
	Q_OBJECT
	public:
		SkeletonView();
		void setWorldEditor(Lumix::WorldEditor& editor);

	private:
		void entitySelected(const Lumix::Array<Lumix::Entity>& entities);
		void viewModel(Lumix::Model* model);
		QTreeWidgetItem* createBoneListItemWidget(Lumix::Model* model, const Lumix::Model::Bone& bone);

	private:
		Lumix::WorldEditor* m_editor;
		QTreeWidget* m_tree_widget;
};