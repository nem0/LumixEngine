#pragma once


#include "animation/animation_system.h"
#include "animation/state_machine.h"
#include "engine/array.h"
#include "imgui/imgui.h"


namespace Lumix
{

class ResourceManagerBase;
namespace Anim { class ControllerResource; } 


namespace AnimEditor
{


struct IAnimationEditor;
class Container;
class ControllerResource;
class Edge;
struct EntryNode;


class Component
{
public:
	Component(Anim::Component* _engine_cmp, Container* parent, ControllerResource& controller)
		: engine_cmp(_engine_cmp)
		, m_parent(parent)
		, m_controller(controller)
	{
	}

	virtual ~Component();
	virtual bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) = 0;
	virtual void onGUI() {}
	virtual void serialize(OutputBlob& blob) = 0;
	virtual void deserialize(InputBlob& blob) = 0;
	virtual bool hitTest(const ImVec2& on_canvas_pos) const { return false; }
	virtual bool isNode() const = 0;
	virtual bool isContainer() const { return false; }
	virtual void drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos) {}
	Container* getParent() { return m_parent; }
	virtual void compile() {}
	virtual void debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Anim::ComponentInstance* runtime) {}
	virtual Component* getByUID(int uid) { return engine_cmp && uid == engine_cmp->uid ? this : nullptr; }
	virtual void debugInside(ImDrawList* draw,
		const ImVec2& canvas_screen_pos,
		Anim::ComponentInstance* runtime,
		Container* current)
	{
	}
	ControllerResource& getController() { return m_controller; }

	Anim::Component* engine_cmp;

protected:
	Container* m_parent;
	ControllerResource& m_controller;
};


class Node : public Component
{
public:
	Node(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);
	~Node();

	bool isNode() const override { return true; }
	bool hitTest(const ImVec2& on_canvas_pos) const override;
	void onGUI() override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob) override;
	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) override;
	void addEdge(Edge* edge) { m_edges.push(edge); }
	void addInEdge(Edge* edge) { m_in_edges.push(edge); }
	void removeEdge(Edge* edge) { m_edges.eraseItemFast(edge); }
	void removeInEdge(Edge* edge) { m_in_edges.eraseItemFast(edge); }
	void removeEvent(int index);

public:
	ImVec2 pos;
	ImVec2 size;
	StaticString<64> name;

protected:
	Array<Edge*> m_edges;
	Array<Edge*> m_in_edges;
	IAllocator& m_allocator;
};


class Container : public Node
{
public:
	Container(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);
	~Container();
	Component* childrenHitTest(const ImVec2& pos);
	Component* getChildByUID(int uid);
	Component* getSelectedComponent() const { return m_selected_component; }
	void deserialize(InputBlob& blob) override;
	void serialize(OutputBlob& blob) override;
	void compile() override;
	virtual Component* getByUID(int uid) override;
	virtual void dropSlot(const char* name, u32 slot, const ImVec2& canvas_screen_pos) {}
	virtual void removeChild(Component* component);
	bool isContainer() const override { return true; }
	void createEdge(int from_uid, int to_uid, int edge_uid);
	void destroyChild(int child_uid);
	virtual void createNode(Anim::Node::Type type, int uid, const ImVec2& pos) = 0;

protected:
	Array<Component*> m_editor_cmps;
	Component* m_selected_component;
};



class Edge : public Component
{
public:
	Edge(Anim::Edge* engine_cmp, Container* parent, ControllerResource& controller);
	~Edge();

	bool isNode() const override { return false; }

	void onGUI() override;
	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob) override;
	void compile() override;
	bool hitTest(const ImVec2& on_canvas_pos) const override;
	const char* getExpression() const { return m_expression; }
	void debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Anim::ComponentInstance* runtime) override;

private:
	Node* m_from;
	Node* m_to;
	StaticString<128> m_expression;
};


class AnimationNode : public Node
{
public:
	AnimationNode(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);

	void compile() override;
	void onGUI() override;
	void debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Anim::ComponentInstance* runtime) override;
	void deserialize(InputBlob& blob) override;

	int root_rotation_input = -1;
};


class Blend1DNode : public Container
{
public:
	struct RootEdge;
	struct RootNode : public Node
	{
		RootNode(Container* parent, ControllerResource& controller);

		Array<RootEdge*> edges;
	};

public:
	Blend1DNode(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);

	void compile() override;
	void onGUI() override;
	void debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Anim::ComponentInstance* runtime) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob) override;
	void drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos) override;
	RootNode* getRootNode() const { return m_root_node; }
	void removeChild(Component* component) override;
	void dropSlot(const char* name, u32 slot, const ImVec2& canvas_screen_pos) override;
	void debugInside(ImDrawList* draw,
		const ImVec2& canvas_screen_pos,
		Anim::ComponentInstance* runtime,
		Container* current) override;

private:
	void createNode(Anim::Component::Type type, int uid, const ImVec2& pos) override;
	RootEdge* createRootEdge(Node* node);

private:
	enum MouseStatus
	{
		NONE,
		DOWN_LEFT,
		DOWN_RIGHT,
		DRAG_NODE,
		NEW_EDGE
	} m_mouse_status;
	int m_input = -1;
	Node* m_drag_source = nullptr;
	RootNode* m_root_node;
	Component* m_context_cmp = nullptr;
};


struct EntryNode : public Node
{
	EntryNode(Container* parent, ControllerResource& controller);

	Array<struct EntryEdge*> entries;
};


class StateMachine : public Container
{
public:
	StateMachine(Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);

	void drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos) override;
	void onGUI() override;
	void debugInside(ImDrawList* draw,
		const ImVec2& canvas_screen_pos,
		Anim::ComponentInstance* runtime,
		Container* current) override;
	void debug(ImDrawList* draw, const ImVec2& canvas_screen_pos, Anim::ComponentInstance* runtime) override;
	void deserialize(InputBlob& blob) override;
	void serialize(OutputBlob& blob) override;
	EntryNode* getEntryNode() const { return m_entry_node; }
	void compile() override;
	void removeEntry(EntryEdge& entry);
	void dropSlot(const char* name, u32 slot, const ImVec2& canvas_screen_pos) override;

private:
	void createNode(Anim::Component::Type type, int uid, const ImVec2& pos) override;
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
	ControllerResource(IAnimationEditor& editor,
		ResourceManagerBase& manager,
		IAllocator& allocator);
	~ControllerResource();

	void serialize(OutputBlob& blob);
	bool deserialize(InputBlob& blob, Engine& engine, IAllocator& allocator);
	Component* getRoot() { return m_root; }
	Array<string>& getAnimationSlots() { return m_animation_slots; }
	IAllocator& getAllocator() { return m_allocator; }
	Anim::ControllerResource* getEngineResource() { return m_engine_resource; }
	IAnimationEditor& getEditor() { return m_editor; }
	int createUID() { ++m_last_uid; return m_last_uid; }
	const char* getAnimationSlot(u32 slot_hash) const;
	void createAnimSlot(const char* name, const char* path);
	Component* getByUID(int uid);

private:
	int m_last_uid = 0;
	IAnimationEditor& m_editor;
	IAllocator& m_allocator;
	Component* m_root;
	Anim::ControllerResource* m_engine_resource;
	Array<string> m_animation_slots;
};


} // namespace AnimEditor

} // namespace Lumix