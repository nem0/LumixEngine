#pragma once


#include "engine/array.h"
#include "imgui/imgui.h"
#include "animation/state_machine.h"


namespace Lumix
{
class ResourceManagerBase;
namespace Anim
{
	class ControllerResource;
}
}


namespace AnimEditor
{


class AnimationEditor;
class Container;
class ControllerResource;
class Edge;
struct EntryNode;


class Component
{
public:
	Component(Lumix::Anim::Component* _engine_cmp, Container* parent, ControllerResource& controller)
		: engine_cmp(_engine_cmp)
		, m_parent(parent)
		, m_controller(controller)
	{
	}

	virtual ~Component();
	virtual bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) = 0;
	virtual void onGUI() {}
	virtual void serialize(Lumix::OutputBlob& blob) = 0;
	virtual void deserialize(Lumix::InputBlob& blob) = 0;
	virtual bool hitTest(const ImVec2& on_canvas_pos) const { return false; }
	virtual bool isNode() const = 0;
	virtual bool isContainer() const { return false; }
	virtual void drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos) {}
	Container* getParent() { return m_parent; }
	virtual void compile() {}
	virtual void debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Lumix::Anim::ComponentInstance* runtime) {}
	virtual void debugInside(ImDrawList* draw,
		const ImVec2& canvas_screen_pos,
		Lumix::Anim::ComponentInstance* runtime,
		Container* current)
	{
	}

	Lumix::Anim::Component* engine_cmp;

protected:
	Container* m_parent;
	ControllerResource& m_controller;
};


class Node : public Component
{
public:
	Node(Lumix::Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);
	~Node();

	bool isNode() const override { return true; }
	bool hitTest(const ImVec2& on_canvas_pos) const override;
	void onGUI() override;
	void serialize(Lumix::OutputBlob& blob) override;
	void deserialize(Lumix::InputBlob& blob) override;
	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) override;
	void addEdge(Edge* edge) { m_edges.push(edge); }
	void addInEdge(Edge* edge) { m_in_edges.push(edge); }
	void removeEdge(Edge* edge) { m_edges.eraseItemFast(edge); }
	void removeInEdge(Edge* edge) { m_in_edges.eraseItemFast(edge); }
	void removeEvent(int index);

public:
	ImVec2 pos;
	ImVec2 size;
	Lumix::StaticString<64> name;

protected:
	Lumix::Array<Edge*> m_edges;
	Lumix::Array<Edge*> m_in_edges;
	Lumix::IAllocator& m_allocator;
};


class Container : public Node
{
public:
	Container(Lumix::Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);
	~Container();
	Component* childrenHitTest(const ImVec2& pos);
	Component* getChildByUID(int uid);
	Component* getSelectedComponent() const { return m_selected_component; }
	void deserialize(Lumix::InputBlob& blob) override;
	void serialize(Lumix::OutputBlob& blob) override;
	void compile() override;

	virtual void dropSlot(const char* name, Lumix::u32 slot, const ImVec2& canvas_screen_pos) {}
	virtual void removeChild(Component* component);
	bool isContainer() const override { return true; }
	
protected:
	Lumix::Array<Component*> m_editor_cmps;
	Component* m_selected_component;
};



class Edge : public Component
{
public:
	Edge(Lumix::Anim::Edge* engine_cmp, Container* parent, ControllerResource& controller);
	~Edge();

	bool isNode() const override { return false; }

	void onGUI() override;
	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) override;
	void serialize(Lumix::OutputBlob& blob) override;
	void deserialize(Lumix::InputBlob& blob) override;
	void compile() override;
	bool hitTest(const ImVec2& on_canvas_pos) const override;
	const char* getExpression() const { return m_expression; }
	void debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Lumix::Anim::ComponentInstance* runtime) override;

private:
	Node* m_from;
	Node* m_to;
	char m_expression[128];
};


class AnimationNode : public Node
{
public:
	AnimationNode(Lumix::Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);

	void compile() override;
	void onGUI() override;
	void debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Lumix::Anim::ComponentInstance* runtime) override;

	int root_rotation_input = -1;
};


struct EntryNode : public Node
{
	EntryNode(Container* parent, ControllerResource& controller);

	Lumix::Array<struct EntryEdge*> entries;
};


class StateMachine : public Container
{
public:
	StateMachine(Lumix::Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);

	void drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos) override;
	void onGUI() override;
	void debugInside(ImDrawList* draw,
		const ImVec2& canvas_screen_pos,
		Lumix::Anim::ComponentInstance* runtime,
		Container* current) override;
	void debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Lumix::Anim::ComponentInstance* runtime) override;
	void deserialize(Lumix::InputBlob& blob) override;
	void serialize(Lumix::OutputBlob& blob) override;
	EntryNode* getEntryNode() const { return m_entry_node; }
	void compile() override;
	void removeChild(Component* component) override;
	void dropSlot(const char* name, Lumix::u32 slot, const ImVec2& canvas_screen_pos) override;

private:
	void createState(Lumix::Anim::Component::Type type, const ImVec2& pos);
	EntryEdge* createEntryEdge(Node* node);

private:
	enum MouseStatus
	{
		NONE,
		DOWN_LEFT,
		DOWN_RIGHT,
		DRAG_NODE,
		NEW_EDGE
	} m_mouse_status;
	Node* m_drag_source;
	EntryNode* m_entry_node;
	Component* m_context_cmp;
};


class ControllerResource
{
public:
	ControllerResource(AnimationEditor& editor, Lumix::ResourceManagerBase& manager, Lumix::IAllocator& allocator);
	~ControllerResource();

	void serialize(Lumix::OutputBlob& blob);
	bool deserialize(Lumix::InputBlob& blob, Lumix::Engine& engine, Lumix::IAllocator& allocator);
	Component* getRoot() { return m_root; }
	Lumix::Array<Lumix::string>& getAnimationSlots() { return m_animation_slots; }
	Lumix::IAllocator& getAllocator() { return m_allocator; }
	Lumix::Anim::ControllerResource* getEngineResource() { return m_engine_resource; }
	AnimationEditor& getEditor() { return m_editor; }
	int createUID() { ++m_last_uid; return m_last_uid; }
	const char* getAnimationSlot(Lumix::u32 slot_hash) const;
	void createAnimSlot(const char* name, const char* path);

private:
	int m_last_uid = 0;
	AnimationEditor& m_editor;
	Lumix::IAllocator& m_allocator;
	Component* m_root;
	Lumix::Anim::ControllerResource* m_engine_resource;
	Lumix::Array<Lumix::string> m_animation_slots;
};


} // namespace AnimEditor