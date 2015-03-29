#include "animation_inputs.h"
#include "animation_editor.h"
#include "animation_editor_commands.h"
#include "animator.h"
#include <qlayout.h>
#include <qmenu.h>
#include <qmetaobject.h>
#include <qtableview.h>

AnimationInputs::AnimationInputs(AnimationEditor& editor)
	: m_editor(editor)
{
	setWindowTitle("Inputs");
	setObjectName("animationEditorInputs");
	QWidget* widget = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);

	m_table_view = new QTableView(widget);
	layout->addWidget(m_table_view);
	setWidget(widget);

	m_table_view->setContextMenuPolicy(Qt::CustomContextMenu);
	m_table_view->setModel(m_editor.getAnimator()->getInputModel());
	m_editor.connect(&m_editor, &AnimationEditor::animatorCreated, [this](){
		m_table_view->setModel(m_editor.getAnimator()->getInputModel());
	});
	connect(m_table_view, &QWidget::customContextMenuRequested, this, &AnimationInputs::showContextMenu);

	m_table_view->setItemDelegate(new AnimatorInputTypeDelegate());
}


void AnimationInputs::showContextMenu(const QPoint& pos)
{
	QModelIndex model_index = m_table_view->indexAt(pos);
	QMenu* menu = new QMenu();
	QAction* create_action = menu->addAction("Create");
	QAction* destroy_action = model_index.isValid() ?  menu->addAction("Remove") : NULL;
	QAction* selected_action = menu->exec(mapToGlobal(pos));
	if (selected_action == create_action)
	{
		m_editor.executeCommand(new CreateAnimationInputCommand(m_editor.getAnimator()));
	}
	else if (selected_action = destroy_action)
	{
		m_editor.executeCommand(new DestroyAnimationInputCommand(m_editor.getAnimator(), model_index.row()));
	}
}
