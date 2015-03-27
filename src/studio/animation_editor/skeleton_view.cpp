#include "skeleton_view.h"
#include "editor/world_editor.h"
#include "graphics/render_scene.h"


SkeletonView::SkeletonView()
{
	setObjectName("skeletonView");
	setWindowTitle("Skeleton");
	m_editor = NULL;
	m_tree_widget = new QTreeWidget(this);
	m_tree_widget->setHeaderLabel("Bone");
	setWidget(m_tree_widget);
}


void SkeletonView::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
	editor.entitySelected().bind<SkeletonView, &SkeletonView::entitySelected>(this);
}


void SkeletonView::entitySelected(const Lumix::Array<Lumix::Entity>& entities)
{
	if (!entities.empty())
	{
		Lumix::Component cmp = m_editor->getComponent(entities[0], crc32("renderable"));
		if (cmp.isValid())
		{
			Lumix::Model* model = static_cast<Lumix::RenderScene*>(cmp.scene)->getRenderableModel(cmp);
			if (model)
			{
				viewModel(model);
			}
		}
	}
}


QTreeWidgetItem* SkeletonView::createBoneListItemWidget(Lumix::Model* model, const Lumix::Model::Bone& parent_bone)
{
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << parent_bone.name.c_str());
	for (int i = 0; i < model->getBoneCount(); ++i)
	{
		const Lumix::Model::Bone& bone = model->getBone(i);
		if (bone.parent_idx >= 0 && &model->getBone(bone.parent_idx) == &parent_bone)
		{
			item->addChild(createBoneListItemWidget(model, bone));
		}
	}
	return item;
}


void SkeletonView::viewModel(Lumix::Model* model)
{
	ASSERT(model);
	m_tree_widget->clear();
	for (int i = 0; i < model->getBoneCount(); ++i)
	{
		const Lumix::Model::Bone& bone = model->getBone(i);
		if (bone.parent_idx < 0)
		{
			m_tree_widget->addTopLevelItem(createBoneListItemWidget(model, bone));
		}
	}
	m_tree_widget->expandAll();
}