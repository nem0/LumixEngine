#pragma once


#include <cstdint>
#include <qdockwidget.h>
#include <qundostack.h>


namespace Lumix
{
	struct Component;
	class WorldEditor;
}


class AnimationEditor;
class AnimationInputs;
class AnimationNode;
class AnimatorEdge;
class AnimatorNodeContent;
class Animator;
class AnimatorNode;
class MainWindow;
class PropertyView;
class QMenu;
class QAction;
class ScriptCompiler;
class SkeletonView;


class AnimationGraphView : public QWidget
{
	Q_OBJECT
	public:
		enum class MouseMode
		{
			NONE,
			DRAGGING,
			EDGE
		};

	public:
		AnimationGraphView(AnimationEditor& editor);

		void setNode(AnimatorNode* node) { m_node = node; }

	private:
		virtual void paintEvent(QPaintEvent*) override;
		virtual void mousePressEvent(QMouseEvent*) override;
		virtual void mouseMoveEvent(QMouseEvent*) override;
		virtual void mouseReleaseEvent(QMouseEvent*) override;
		void drawGrid(QPainter& painter);
		void drawNodes(QPainter& painter);
		void drawNewEdge(QPainter& painter);
		void selectNode(AnimatorNode* node);
		void selectEdge(AnimatorEdge* node);

	private slots:
		void showContextMenu(const QPoint& pos);

	private:
		AnimatorEdge* m_selected_edge;
		AnimatorNode* m_node;
		AnimatorNode* m_mouse_node;
		MouseMode m_mouse_mode;
		QPoint m_last_mouse_position;
		AnimationEditor& m_editor;
};



class AnimationEditor : public QDockWidget
{
	Q_OBJECT
	public:
		AnimationEditor(MainWindow& main_window);

		void setWorldEditor(Lumix::WorldEditor& editor);
		void setComponent(const Lumix::Component& component);
		void update(float time_delta);
		Animator* getAnimator() { return m_animator; }
		void executeCommand(QUndoCommand* command);
		PropertyView& getPropertyView() { return m_property_view; }
		AnimatorNodeContent* createContent(AnimatorNode& node, uint32_t content_type);
		QUndoStack& getUndoStack() { return m_undo_stack; }
	
	signals:
		void animatorCreated();

	private:
		class DockInfo
		{
			public:
				QDockWidget* m_widget;
				QAction* m_action;
		};

	private:
		void addMenu(MainWindow& main_window);
		void addEditorDock(Qt::DockWidgetArea area, QDockWidget* widget);

	private slots:
		void onCompileAction();
		void onRunAction();
		void onSaveAction();
		void onSaveAsAction();
		void onLoadAction();

	private:
		MainWindow& m_main_window;
		QUndoStack m_undo_stack;
		Animator* m_animator;
		AnimationGraphView* m_animation_graph_view;
		AnimationInputs* m_inputs;
		SkeletonView* m_skeleton_view;
		PropertyView& m_property_view;
		Lumix::WorldEditor* m_editor;
		ScriptCompiler& m_compiler;
		QList<DockInfo> m_dock_infos;
		QMenu* m_view_menu;
		QAction* m_compile_action;
		QAction* m_run_action;
		QAction* m_save_action;
		QAction* m_save_as_action;
		QAction* m_load_action;
		QAction* m_undo_action;
		QAction* m_redo_action;
};