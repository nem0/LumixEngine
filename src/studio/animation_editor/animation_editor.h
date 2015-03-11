#pragma once


#include <qdockwidget.h>
#include <qundostack.h>


namespace Lumix
{
	struct Component;
	class WorldEditor;
}


class AnimationEditor;
class AnimationNode;
class Animator;
class AnimatorNode;


class AnimationGraphView : public QWidget
{
	Q_OBJECT
	public:
		enum class MouseMode
		{
			NONE,
			DRAGGING
		};

	public:
		AnimationGraphView(AnimationEditor& editor);

		virtual void paintEvent(QPaintEvent*) override;
		virtual void mousePressEvent(QMouseEvent*) override;
		virtual void mouseMoveEvent(QMouseEvent*) override;

	private:
		void drawGrid(QPainter& painter);
		void drawNodes(QPainter& painter);

	private slots:
		void showContextMenu(const QPoint& pos);

	private:
		AnimatorNode* m_node;
		AnimatorNode* m_dragged_node;
		MouseMode m_mouse_mode;
		QPoint m_last_mouse_position;
		AnimationEditor& m_editor;
};



class AnimationEditor : public QDockWidget
{
	Q_OBJECT
	public:
		AnimationEditor();

		void setWorldEditor(Lumix::WorldEditor& editor);
		void setComponent(const Lumix::Component& component);
		void update(float time_delta);
		Animator* getAnimator() { return m_animator; }
		void executeCommand(QUndoCommand* command);

	private:
		QUndoStack m_undo_stack;
		Animator* m_animator;
		AnimationGraphView* m_animation_graph_view;
};