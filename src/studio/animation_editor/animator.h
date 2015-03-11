#pragma once 


#include <qlibrary.h>
#include <qlist.h>
#include <qpainter.h>
#include <qpoint.h>
#include <qstring.h>


class AnimationEditor;
class Animator;
class AnimatorNode;


namespace Lumix
{
	class AnimationManager;
	class Model;
	class Pose;
	class WorldEditor;
}


class AnimatorNodeContent
{
	public:
		AnimatorNodeContent(AnimatorNode* node) : m_node(node) {}

		virtual void paint(QPainter& painter) = 0;
		virtual void paintContainer(QPainter& painter) = 0;
		virtual AnimatorNode* getNodeAt(int x, int y) = 0;
		virtual void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos) = 0;
		virtual bool hitTest(int x, int y) = 0;
		virtual int getChildCount() = 0;
		AnimatorNode* getNode() { return m_node; }
		virtual QString generateCode() = 0;

	private:
		friend class Animator;
		virtual void addChild(AnimatorNode*) { Q_ASSERT(false); }
		virtual void removeChild(AnimatorNode*) { Q_ASSERT(false); }

	protected:
		AnimatorNode* m_node;
};


class AnimationNodeContent : public AnimatorNodeContent
{
	public:
		AnimationNodeContent(AnimatorNode* node) : AnimatorNodeContent(node), m_animation_path("models/animals/out.ani") {}

		virtual bool hitTest(int x, int y) override;
		virtual AnimatorNode* getNodeAt(int x, int y) override;
		virtual void paint(QPainter& painter) override;
		virtual void paintContainer(QPainter& painter) override;
		virtual void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos) override;
		virtual int getChildCount() override { return 0; }
		virtual QString generateCode() override;
		void setAnimationPath(const char* path) { m_animation_path = path; }
		QString getAnimationPath() const { return m_animation_path; }

	private:
		QString m_animation_path;
};



class StateMachineNodeContent : public AnimatorNodeContent
{
	public:
		StateMachineNodeContent(AnimatorNode* node) : AnimatorNodeContent(node), m_default_uid(0) {}
		~StateMachineNodeContent();

		virtual bool hitTest(int x, int y) override;
		virtual AnimatorNode* getNodeAt(int x, int y) override;
		virtual void paint(QPainter& painter) override;
		virtual void paintContainer(QPainter& painter) override;
		virtual void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos) override;
		virtual int getChildCount() override { return m_children.size(); }
		virtual QString generateCode() override;

	private:
		virtual void removeChild(AnimatorNode* node) override;
		virtual void addChild(AnimatorNode* node) override { m_children.push_back(node); }

	private:
		QList<AnimatorNode*> m_children;
		int m_default_uid;
};


class AnimatorNode
{
	public:
		int getUID() const { return m_uid; }
		void paintContainer(QPainter& painter);
		void paintContent(QPainter& painter);
		void setContent(AnimatorNodeContent* content) { m_content = content; }
		AnimatorNodeContent* getContent() { return m_content; }

		AnimatorNode* getContentNodeAt(int x, int y);
		void setName(const char* name) { m_name = name; }
		const QString& getName() const { return m_name; }
		QPoint getPosition() { return m_position; }
		void setPosition(const QPoint& position) { m_position = position; }
		void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos);
		AnimatorNode* getParent() { return m_parent; }

	private:
		friend class Animator;
		AnimatorNode(int uid, AnimatorNode* parent) : m_uid(uid), m_parent(parent) {}

	protected:
		int m_uid;
		QString m_name;
		QPoint m_position;
		AnimatorNodeContent* m_content;
		AnimatorNode* m_parent;
};


class Animator
{
	public:
		Animator();

		void setWorldEditor(Lumix::WorldEditor& editor);
		AnimatorNode* getRoot() { return m_root; }
		AnimatorNode* createNode(AnimatorNode* parent);
		void destroyNode(int uid);
		AnimatorNode* getNode(int uid);
		bool compile(const QString& base_path);
		void run();
		void update(float time_delta);

	private:
		typedef void* (*CreateFunction)();
		typedef void (*UpdateFunction)(void*, Lumix::Model&, Lumix::Pose&, float);
		typedef void (*AnimationManagerSetter)(Lumix::AnimationManager*);

	private:
		int m_last_uid;
		AnimatorNode* m_root;
		QList<AnimatorNode*> m_nodes;
		QLibrary m_library;
		Lumix::WorldEditor* m_editor;
		UpdateFunction m_update_function;
		void* m_object;
};