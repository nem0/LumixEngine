#include "animable_component_plugin.h"
#include "animation_editor.h"
#include "core/crc32.h"
#include "universe/component.h"
#include <qlayout.h>
#include <qpushbutton.h>
#include <qtreewidget.h>


uint32_t AnimableComponentPlugin::getType()
{
	return crc32("animable");
}


void AnimableComponentPlugin::createEditor(QTreeWidgetItem* component_item, const Lumix::Component& component)
{
	QTreeWidgetItem* tools_item = new QTreeWidgetItem(QStringList() << "Tools");
	component_item->addChild(tools_item);
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* edit_button = new QPushButton("Edit", widget);
	edit_button->connect(edit_button, &QPushButton::clicked, [this, &component]() {
		m_animation_editor.setComponent(component);
		m_animation_editor.show();
	});
	layout->addWidget(edit_button);
	component_item->treeWidget()->setItemWidget(tools_item, 1, widget);
}
