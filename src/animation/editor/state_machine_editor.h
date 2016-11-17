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
struct Container;
class ControllerResource;
struct Edge;


struct Component
{
	Component(Container* parent, Lumix::Anim::Component* _engine_cmp) : engine_cmp(_engine_cmp), m_parent(parent) {}

	virtual ~Component() {}
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

	Lumix::Anim::Component* engine_cmp;

protected:
	Container* m_parent;
};


class Node : public Component
{
public:
	Node(Lumix::Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);

	bool isNode() const override { return true; }
	bool hitTest(const ImVec2& on_canvas_pos) const override;
	void onGUI() override;
	void serialize(Lumix::OutputBlob& blob) override;
	void deserialize(Lumix::InputBlob& blob) override;
	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) override;
	const char* getName() { return m_name; }

public:
	ImVec2 pos;
	ImVec2 size;

protected:
	ControllerResource& m_controller;
	char m_name[64];
	Lumix::Array<Edge*> edges;
	Lumix::IAllocator& m_allocator;
};


struct Container : public Node
{
	Container(Lumix::Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);
	Component* childrenHitTest(const ImVec2& pos);
	Component* getChildByUID(int uid);
	void deserialize(Lumix::InputBlob& blob) override;
	void serialize(Lumix::OutputBlob& blob) override;
	void compile() override;

	bool isContainer() const override { return true; }
	
	Lumix::Array<Component*> m_editor_cmps;
	Component* m_selected_component;
};



struct Edge : public Component
{
public:
	Edge(Lumix::Anim::Edge* engine_cmp, Container* parent, ControllerResource& controller);

	bool isNode() const override { return false; }

	void onGUI() override;
	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, bool selected) override;
	void serialize(Lumix::OutputBlob& blob) override;
	void deserialize(Lumix::InputBlob& blob) override;
	void compile() override;
	bool hitTest(const ImVec2& on_canvas_pos) const override;
	const char* getExpression() const { return m_expression; }

private:
	ControllerResource& m_controller;
	Node* m_from;
	Node* m_to;
	char m_expression[128];
};


class SimpleAnimationNode : public Node
{
public:
	SimpleAnimationNode(Lumix::Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);

	void onGUI() override;

private:
	char animation[64];
};


class StateMachine : public Container
{
public:
	StateMachine(Lumix::Anim::Component* engine_cmp, Container* parent, ControllerResource& controller);

	void drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos) override;
	void onGUI() override;

private:
	void createState(Lumix::Anim::Component::Type type);

private:
	bool m_is_making_line = false;
};


class ControllerResource
{
public:
	ControllerResource(AnimationEditor& editor, Lumix::ResourceManagerBase& manager, Lumix::IAllocator& allocator);

	void serialize(Lumix::OutputBlob& blob);
	void deserialize(Lumix::InputBlob& blob, Lumix::Engine& engine, Lumix::IAllocator& allocator);
	Component* getRoot() { return m_root; }
	Lumix::Array<Lumix::string>& getAnimationSlots() { return m_animation_slots; }
	Lumix::IAllocator& getAllocator() { return m_allocator; }
	Lumix::Anim::ControllerResource* getEngineResource() { return m_engine_resource; }
	AnimationEditor& getEditor() { return m_editor; }
	int createUID() { ++m_last_uid; return m_last_uid; }

private:
	int m_last_uid = 0;
	AnimationEditor& m_editor;
	Lumix::IAllocator& m_allocator;
	Component* m_root;
	Lumix::Anim::ControllerResource* m_engine_resource;
	Lumix::Array<Lumix::string> m_animation_slots;
};


} // namespace AnimEditor