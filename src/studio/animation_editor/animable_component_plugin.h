#pragma once


#include <QWidget>
#include "property_view.h"


class AnimationEditor;


class AnimableComponentPlugin : public QObject, public PropertyView::IEntityComponentPlugin
{
	Q_OBJECT
public:
	AnimableComponentPlugin(AnimationEditor& animation_editor)
		: m_animation_editor(animation_editor)
	{}

	virtual uint32_t getType() override;
	virtual void createEditor(QTreeWidgetItem* component_item, const Lumix::Component& component) override;
	virtual void onPropertyViewCleared() override {}

private:
	AnimationEditor& m_animation_editor;
};