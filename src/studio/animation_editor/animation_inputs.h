#pragma once


#include <qdockwidget.h>


class AnimationEditor;


class AnimationInputs : public QDockWidget
{
	Q_OBJECT
	public:
		AnimationInputs(AnimationEditor& editor);

	private:
		void showContextMenu(const QPoint& pos);

	private:
		AnimationEditor& m_editor;
		class QTableView* m_table_view;
};