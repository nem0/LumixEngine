#pragma once 


#include <cstdint>
#include <qlibrary.h>
#include <qlist.h>
#include <qpainter.h>
#include <qpoint.h>
#include <qstring.h>


class AnimationEditor;
class Animator;
class AnimatorNode;
class PropertyView;
class ScriptCompiler;


namespace Lumix
{
	class AnimationManager;
	class InputBlob;
	class Model;
	class OutputBlob;
	class Pose;
	class WorldEditor;
}


class AnimatorEdge
{
	public:
		AnimatorEdge(AnimatorNode* from, AnimatorNode* to) : m_from(from), m_to(to) {}
		AnimatorNode* getFrom() const { return m_from; }
		AnimatorNode* getTo() const { return m_to; }
		QPoint getFromPosition() const;
		QPoint getToPosition() const;

	private:
		AnimatorNode* m_from;
		AnimatorNode* m_to;
};


class AnimatorNodeContent
{
	public:
		AnimatorNodeContent(AnimatorNode* node) : m_node(node) {}

		virtual void paint(QPainter& painter) = 0;
		virtual void paintContainer(QPainter& painter) = 0;
		virtual AnimatorNode* getNodeAt(int x, int y) const = 0;
		virtual void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos) = 0;
		virtual bool hitTest(int x, int y) const = 0;
		virtual int getChildCount() const = 0;
		AnimatorNode* getNode() const { return m_node; }
		virtual QString generateCode() = 0;
		virtual void fillPropertyView(PropertyView& view) = 0;
		virtual uint32_t getType() const = 0;
		virtual void serialize(Lumix::OutputBlob& blob) = 0;
		virtual void deserialize(AnimationEditor& editor, Lumix::InputBlob& blob) = 0;

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
		AnimationNodeContent(AnimatorNode* node) : AnimatorNodeContent(node) {}

		virtual bool hitTest(int x, int y) const override;
		virtual AnimatorNode* getNodeAt(int x, int y) const override;
		virtual void paint(QPainter& painter) override;
		virtual void paintContainer(QPainter& painter) override;
		virtual void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos) override;
		virtual int getChildCount() const override { return 0; }
		virtual QString generateCode() override;
		void setAnimationPath(const char* path) { m_animation_path = path; }
		QString getAnimationPath() const { return m_animation_path; }
		virtual void fillPropertyView(PropertyView& view) override;
		virtual uint32_t getType() const override;
		virtual void serialize(Lumix::OutputBlob& blob) override;
		virtual void deserialize(AnimationEditor& editor, Lumix::InputBlob& blob) override;

	private:
		QString m_animation_path;
};



class StateMachineNodeContent : public AnimatorNodeContent
{
	public:
		StateMachineNodeContent(AnimatorNode* node) : AnimatorNodeContent(node), m_default_uid(0) {}
		~StateMachineNodeContent();

		virtual bool hitTest(int x, int y) const override;
		virtual AnimatorNode* getNodeAt(int x, int y) const override;
		virtual void paint(QPainter& painter) override;
		virtual void paintContainer(QPainter& painter) override;
		virtual void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos) override;
		virtual int getChildCount() const override { return m_children.size(); }
		virtual QString generateCode() override;
		virtual void fillPropertyView(PropertyView&) override {  }
		virtual uint32_t getType() const override;
		virtual void serialize(Lumix::OutputBlob& blob) override;
		virtual void deserialize(AnimationEditor& editor, Lumix::InputBlob& blob) override;
		void addEdge(AnimatorNode* from, AnimatorNode* to);

	private:
		void drawEdges(QPainter& painter);
		virtual void removeChild(AnimatorNode* node) override;
		virtual void addChild(AnimatorNode* node) override { m_children.push_back(node); }

	private:
		QList<AnimatorEdge> m_edges;
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
		QPoint getPosition() const { return m_position; }
		QPoint getCenter() const;
		void setPosition(const QPoint& position) { m_position = position; }
		void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos);
		AnimatorNode* getParent() { return m_parent; }
		void serialize(Lumix::OutputBlob& blob);
		void deserialize(AnimationEditor& editor, Lumix::InputBlob& blob);

	private:
		friend class Animator;
		AnimatorNode(int uid, AnimatorNode* parent) : m_uid(uid), m_parent(parent), m_content(NULL) {}

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
		Animator(ScriptCompiler& compiler);

		void setPath(const QString& path);
		QString getPath() const { return m_path; }
		bool isValidPath() const { return !m_path.isEmpty(); }
		void setWorldEditor(Lumix::WorldEditor& editor);
		AnimatorNode* getRoot() { return m_root; }
		AnimatorNode* createNode(AnimatorNode* parent);
		void destroyNode(int uid);
		AnimatorNode* getNode(int uid);
		bool compile();
		void run();
		void update(float time_delta);
		void serialize(Lumix::OutputBlob& blob);
		void deserialize(AnimationEditor& editor, Lumix::InputBlob& blob);

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
		ScriptCompiler& m_compiler;
		QString m_path;
};