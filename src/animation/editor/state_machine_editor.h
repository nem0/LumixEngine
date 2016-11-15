#pragma once
#include "engine/array.h"
#include "imgui/imgui.h"


namespace Lumix
{
class Engine;
class InputBlob;
class OutputBlob;
class ResourceManagerBase;
namespace Anim
{
	struct Component;
	class ControllerResource;
	struct Edge;
}
}


namespace AnimEditor
{


struct Container;
struct Edge;


struct Component
{
	Component(Lumix::Anim::Component* _engine_cmp) : engine_cmp(_engine_cmp) {}

	virtual ~Component() {}
	virtual bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, Container* parent, bool selected) = 0;
	virtual void onGUI() {}
	virtual void serialize(Lumix::OutputBlob& blob) = 0;
	virtual void deserialize(Lumix::InputBlob& blob) = 0;
	virtual bool hitTest(const ImVec2& on_canvas_pos) const { return false; }
	virtual bool isNode() const = 0;
	virtual bool isContainer() const { return false; }
	virtual void drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos) {}

	Lumix::Anim::Component* engine_cmp;
};


class Node : public Component
{
public:
	Node(Lumix::Anim::Component* engine_cmp, Container* parent, Lumix::IAllocator& allocator);

	bool isNode() const override { return true; }
	bool hitTest(const ImVec2& on_canvas_pos) const override;
	void onGUI() override;
	void serialize(Lumix::OutputBlob& blob) override;
	void deserialize(Lumix::InputBlob& blob) override;
	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, Container* parent, bool selected) override;

protected:
	void makeNewLineProcess(const ImVec2& canvas_screen_pos, ImDrawList* draw, Container* parent);
	bool dragNodeProcess();

public:
	ImVec2 pos;
	ImVec2 size;

protected:
	char m_name[64];
	Lumix::Array<Edge*> edges;
	Lumix::IAllocator& m_allocator;
	Container* m_parent;
	ImVec2 m_line_from;
	bool m_is_making_line = false;
};


struct Container : public Node
{
	Container(Lumix::Anim::Component* engine_cmp, Container* parent, Lumix::IAllocator& allocator);
	Component* childrenHitTest(const ImVec2& pos);
	Component* getChildByUID(int uid);

	bool isContainer() const override { return true; }
	
	Lumix::Array<Component*> m_editor_cmps;
};



struct Edge : public Component
{
public:
	Edge(Lumix::Anim::Edge* engine_cmp, Container* parent);

	bool isNode() const override { return false; }

	void onGUI() override;
	bool draw(ImDrawList* draw, const ImVec2& canvas_screen_pos, Container*, bool selected) override;
	void serialize(Lumix::OutputBlob& blob) override;
	void deserialize(Lumix::InputBlob& blob) override;
	bool hitTest(const ImVec2& on_canvas_pos) const override;

private:
	Node* m_from;
	Node* m_to;
	Container* m_parent;
	char m_expression[128];
};




struct SimpleAnimationNode : public Node
{
	SimpleAnimationNode(Lumix::Anim::Component* engine_cmp, Container* parent, Lumix::IAllocator& allocator);
	
	void onGUI() override;

	char animation[64];
};


class StateMachine : public Container
{
public:
	StateMachine(Lumix::Anim::Component* engine_cmp, Container* parent, Lumix::IAllocator& allocator);

	void deserialize(Lumix::InputBlob& blob) override;
	void serialize(Lumix::OutputBlob& blob) override;
	void drawInside(ImDrawList* draw, const ImVec2& canvas_screen_pos) override;
	void onGUI() override;

private:
	Component* m_selected_component;
};


class ControllerResource
{
public:
	ControllerResource(Lumix::IAllocator& allocator, Lumix::ResourceManagerBase& manager);


	void serialize(Lumix::OutputBlob& blob);
	void deserialize(Lumix::InputBlob& blob, Lumix::Engine& engine, Lumix::IAllocator& allocator);
	Component* getRoot() { return m_root; }
	Lumix::Array<Lumix::string>& getAnimationSlots() { return m_animation_slots; }

private:
	Component* m_root;
	Lumix::Anim::ControllerResource* m_engine_resource;
	Lumix::Array<Lumix::string> m_animation_slots;
};


} // namespace AnimEditor