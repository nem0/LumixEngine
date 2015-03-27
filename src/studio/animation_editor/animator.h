#pragma once 


#include <cstdint>
#include <qitemdelegate.h>
#include <qlibrary.h>
#include <qlist.h>
#include <qpainter.h>
#include <qpoint.h>
#include <qstring.h>
#include "core/default_allocator.h"


class AnimationEditor;
class Animator;
class AnimatorInputModel;
class AnimatorNode;
class PropertyView;
class QAbstractItemModel;
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


class AnimatorInputType : public QObject
{
	Q_OBJECT
	Q_ENUMS(Type)
	public:
		enum Type
		{
			STRING,
			NUMBER
		};
};


class AnimatorInputTypeDelegate : public QItemDelegate
{
	public:
		QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};


class AnimatorEdge
{
	public:
		AnimatorEdge(int uid, AnimatorNode* from, AnimatorNode* to): m_uid(uid), m_from(from), m_to(to), m_duration(0) {}
		AnimatorNode* getFrom() const { return m_from; }
		AnimatorNode* getTo() const { return m_to; }
		QPoint getFromPosition() const;
		QPoint getToPosition() const;
		QString getCondition() const { return m_condition; }
		void setCondition(const QString& condition) { m_condition = condition; }
		bool hitTest(int x, int y) const;
		void fillPropertyView(PropertyView& view);
		int getUID() const { return m_uid; }
		void setDuration(float duration) { m_duration = duration; }
		float getDuration() const { return m_duration; }

	private:
		int m_uid;
		AnimatorNode* m_from;
		AnimatorNode* m_to;
		QString m_condition;
		float m_duration;
};


class AnimatorNodeContent
{
	public:
		AnimatorNodeContent(AnimatorNode* node) : m_node(node) {}

		virtual void paint(QPainter& painter) = 0;
		virtual void paintContainer(QPainter& painter) = 0;
		virtual AnimatorNode* getNodeAt(int x, int y) const = 0;
		virtual AnimatorEdge* getEdgeAt(int x, int y) const = 0;
		virtual void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos) = 0;
		virtual bool hitTest(int x, int y) const = 0;
		virtual int getChildCount() const = 0;
		AnimatorNode* getNode() const { return m_node; }
		virtual QString generateCode() = 0;
		virtual void fillPropertyView(PropertyView& view) = 0;
		virtual uint32_t getType() const = 0;
		virtual void serialize(Lumix::OutputBlob& blob) = 0;
		virtual void deserialize(AnimationEditor& editor, Lumix::InputBlob& blob) = 0;
		virtual QString generateConditionCode() const { return ""; }
	
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
		virtual AnimatorEdge* getEdgeAt(int, int) const override { return NULL; }
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
		virtual AnimatorEdge* getEdgeAt(int x, int y) const override;
		virtual void paint(QPainter& painter) override;
		virtual void paintContainer(QPainter& painter) override;
		virtual void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos) override;
		virtual int getChildCount() const override { return m_children.size(); }
		virtual QString generateCode() override;
		virtual void fillPropertyView(PropertyView&) override {  }
		virtual uint32_t getType() const override;
		virtual void serialize(Lumix::OutputBlob& blob) override;
		virtual void deserialize(AnimationEditor& editor, Lumix::InputBlob& blob) override;
		virtual QString generateConditionCode() const override;
		
		void createEdge(Animator& animator, AnimatorNode* from, AnimatorNode* to);

	private:
		void drawEdges(QPainter& painter);
		virtual void removeChild(AnimatorNode* node) override;
		virtual void addChild(AnimatorNode* node) override { m_children.push_back(node); }

	private:
		QList<AnimatorEdge*> m_edges;
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
		AnimatorNodeContent* getContent() const { return m_content; }

		AnimatorNode* getContentNodeAt(int x, int y);
		void setName(const char* name) { m_name = name; }
		const QString& getName() const { return m_name; }
		QPoint getPosition() const { return m_position; }
		QPoint getCenter() const;
		void setPosition(const QPoint& position) { m_position = position; }
		void showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos);
		AnimatorNode* getParent() const { return m_parent; }
		void serialize(Lumix::OutputBlob& blob);
		void deserialize(AnimationEditor& editor, Lumix::InputBlob& blob);
		void edgeAdded(AnimatorEdge* edge) { m_out_edges.push_back(edge); }
		void edgeRemoved(AnimatorEdge* edge) { m_out_edges.removeOne(edge); }

	private:
		friend class Animator;
		AnimatorNode(int uid, AnimatorNode* parent) : m_uid(uid), m_parent(parent), m_content(NULL) {}

	protected:
		QList<AnimatorEdge*> m_out_edges;
		int m_uid;
		QString m_name;
		QPoint m_position;
		AnimatorNodeContent* m_content;
		AnimatorNode* m_parent;
};



class Animator
{
	public:
		Animator(AnimationEditor& editor, ScriptCompiler& compiler);

		AnimationEditor& getEditor() { return m_editor; }
		void setPath(const QString& path);
		QString getPath() const { return m_path; }
		bool isValidPath() const { return !m_path.isEmpty(); }
		void setWorldEditor(Lumix::WorldEditor& editor);
		AnimatorNode* getRoot() { return m_root; }
		AnimatorNode* createNode(AnimatorNode* parent);
		AnimatorEdge* createEdge(AnimatorNode* from, AnimatorNode* to);
		void destroyNode(int uid);
		AnimatorNode* getNode(int uid);
		bool compile();
		void run();
		void update(float time_delta);
		void serialize(Lumix::OutputBlob& blob);
		void deserialize(AnimationEditor& editor, Lumix::InputBlob& blob);
		void setInput(unsigned int name_hash, float value);

		void createInput();
		QAbstractItemModel* getInputModel() const;
		Lumix::IAllocator& getAllocator() { return m_allocator; }

	private:
		typedef void* (*CreateFunction)();
		typedef void* (*SetInputFunction)(void*, unsigned int, void*);
		typedef void (*UpdateFunction)(void*, Lumix::Model&, Lumix::Pose&, float);
		typedef bool (*IsReadyFunction)(void*);
		typedef void (*AnimationManagerSetter)(Lumix::AnimationManager*);

	private:
		QString generateInputsCode() const;

	private:
		int m_last_uid;
		AnimatorNode* m_root;
		QList<AnimatorNode*> m_nodes;
		AnimatorInputModel* m_input_model;
		QLibrary m_library;
		Lumix::WorldEditor* m_world_editor;
		UpdateFunction m_update_function;
		IsReadyFunction m_is_ready_function;
		SetInputFunction m_set_input_function;
		void* m_object;
		ScriptCompiler& m_compiler;
		QString m_path;
		Lumix::DefaultAllocator m_allocator;
		AnimationEditor& m_editor;
};