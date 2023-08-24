#pragma once

#include "animation/animation.h"
#include "animation/nodes.h"
#include "controller_editor.h"
#include "engine/array.h"
#include "engine/math.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "editor/utils.h"


namespace Lumix {

struct Model;
struct Pose;

namespace anim_editor {

bool editSlot(const Controller& controller, const char* str_id, u32* slot);

using RuntimeContext = anim::RuntimeContext;
using Type = anim::NodeType;

struct Node : NodeEditorNode {
	Node(Node* parent, Controller& controller, IAllocator& allocator) 
		: m_parent(parent)
		, m_error(allocator)
		, m_nodes(allocator)
		, m_links(allocator)
		, m_controller(controller)
		, m_allocator(allocator)
	{
		m_id = ++m_controller.m_id_generator;
		if (parent) parent->m_nodes.push(this);
	}

	virtual ~Node() {}
	virtual Type type() const = 0;
	virtual void serialize(OutputMemoryStream& stream) const;
	virtual void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version);
	virtual bool onGUI() { return false; }
	virtual bool propertiesGUI() { return false; }
	virtual anim::Node* compile(anim::Controller& controller) = 0;
	virtual bool isValueNode() const { return false; }
	virtual bool isPoseNode() const { return false; }
	
	bool nodeGUI() override;
	void inputSlot(ImGuiEx::PinShape shape = ImGuiEx::PinShape::CIRCLE);
	void outputSlot(ImGuiEx::PinShape shape = ImGuiEx::PinShape::CIRCLE);
	Node* getInput(u32 idx) const;

	static Node* create(Node* parent, Type type, Controller& controller, IAllocator& allocator);

	IAllocator& m_allocator;
	Node* m_parent;
	u8 m_input_counter;
	u8 m_output_counter;
	bool m_selected = false;
	bool m_reachable = false;
	String m_error;
	Array<NodeEditorLink> m_links;
	Array<Node*> m_nodes;
	Controller& m_controller;
};

struct PoseNode : Node {
	PoseNode(Node* parent, Controller& controller, IAllocator& allocator) : Node(parent, controller, allocator) {}
	bool isPoseNode() const override { return true; }
};

struct ValueNode : Node {
	ValueNode(Node* parent, Controller& controller, IAllocator& allocator) : Node(parent, controller, allocator) {}
	bool isValueNode() const override { return true; }
};

struct InputNode : ValueNode {
	InputNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return anim::NodeType::INPUT; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	anim::Node* compile(anim::Controller& controller) override;
	bool onGUI() override;
	bool propertiesGUI() override;

	u32 m_input_index = 0;
};

struct OutputNode final : PoseNode {
	OutputNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return anim::NodeType::OUTPUT; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return false; }
	bool onGUI() override;
	
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	anim::Node* compile(anim::Controller& controller) override;
};

struct TreeNode final : PoseNode {
	TreeNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return anim::NodeType::TREE; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return false; }
	bool onGUI() override;
	bool propertiesGUI() override;
	anim::Node* compile(anim::Controller& controller) override;

	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;

	String m_name;
};

struct SelectNode final : PoseNode {
	SelectNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return anim::NodeType::SELECT; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI() override;
	
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	anim::Node* compile(anim::Controller& controller) override;

	u32 m_options_count = 2;
	Time m_blend_length = Time::fromSeconds(0.3f);
};

struct AnimationNode final : PoseNode {
	AnimationNode(Node* parent, Controller& controller, IAllocator& allocator);
	Type type() const override { return anim::NodeType::ANIMATION; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI() override;
	anim::Node* compile(anim::Controller& controller) override;

	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;

	using Flags = anim::AnimationNode::Flags;

	u32 m_slot = 0;
	u32 m_flags = Flags::LOOPED;
};

struct Blend2DNode final : PoseNode {
	Blend2DNode(Node* parent, Controller& controller, IAllocator& allocator);
	Type type() const override { return anim::NodeType::BLEND2D; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI() override;
	
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	anim::Node* compile(anim::Controller& controller) override;

	void dataChanged();

	using Child = anim::Blend2DNode::Child;
	using Triangle = anim::Blend2DNode::Triangle;

	String m_name;
	Array<Child> m_children;
	Array<Triangle> m_triangles;
	i32 m_hovered_blend2d_child = -1;
};

struct Blend1DNode final : PoseNode {
	Blend1DNode(Node* parent, Controller& controller, IAllocator& allocator);
	Type type() const override { return anim::NodeType::BLEND1D; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI() override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	anim::Node* compile(anim::Controller& controller) override;

	using Child = anim::Blend1DNode::Child;

	String m_name;
	Array<Child> m_children;
};

struct LayersNode final : PoseNode {
	LayersNode(Node* parent, Controller& controller, IAllocator& allocator);
	~LayersNode();
	Type type() const override { return anim::NodeType::LAYERS; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	anim::Node* compile(anim::Controller& controller) override;

	struct Layer {
		Layer(IAllocator& allocator);

		PoseNode* node = nullptr;
		u32 mask = 0;
		String name;
	};

	IAllocator& m_allocator;
	Array<Layer> m_layers;
};

} // namespace anim
} // namespace Lumix
