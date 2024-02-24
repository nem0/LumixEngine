#pragma once

#include "foundation/array.h"
#include "foundation/math.h"
#include "foundation/stream.h"
#include "foundation/string.h"

#include "animation/animation.h"
#include "animation/nodes.h"
#include "controller_editor.h"
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
	virtual bool propertiesGUI(Model& skeleton) { return false; }
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
	virtual anim::Value::Type getReturnType() = 0;
};

struct ConstNode : ValueNode {
	ConstNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return anim::NodeType::CONSTANT; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	anim::Node* compile(anim::Controller& controller) override;
	anim::Value::Type getReturnType() override { return m_value.type; }
	bool onGUI() override;

	anim::Value m_value;
};

struct InputNode : ValueNode {
	InputNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return anim::NodeType::INPUT; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	anim::Node* compile(anim::Controller& controller) override;
	anim::Value::Type getReturnType() override;
	bool onGUI() override;
	bool propertiesGUI(Model& skeleton) override;

	u32 m_input_index = 0;
};

struct PlayRateNode final : PoseNode {
	PlayRateNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return anim::NodeType::PLAYRATE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	anim::Node* compile(anim::Controller& controller) override;
};

struct IKNode final : PoseNode {
	IKNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return anim::NodeType::IK; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI(Model& skeleton) override;
	
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	anim::Node* compile(anim::Controller& controller) override;

	i32 m_leaf_bone = 0;
	u32 m_bones_count = 0;
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
	bool propertiesGUI(Model& skeleton) override;
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
	bool propertiesGUI(Model& skeleton) override;
	
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	anim::Node* compile(anim::Controller& controller) override;

	u32 m_options_count = 2;
	Time m_blend_length = Time::fromSeconds(0.3f);
};

struct MathNode final : ValueNode {
	MathNode(Node* parent, Controller& controller, anim::NodeType type, IAllocator& allocator);

	Type type() const override { return m_type; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	anim::Node* compile(anim::Controller& controller) override;
	anim::Value::Type getReturnType() override;

	const anim::NodeType m_type;
};

struct SwitchNode final : PoseNode {
	SwitchNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return anim::NodeType::SWITCH; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	anim::Node* compile(anim::Controller& controller) override;

	Time m_blend_length = Time::fromSeconds(0.3f);
};

struct AnimationNode final : PoseNode {
	AnimationNode(Node* parent, Controller& controller, IAllocator& allocator);
	Type type() const override { return anim::NodeType::ANIMATION; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI(Model& skeleton) override;
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
	bool propertiesGUI(Model& skeleton) override;
	
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
	bool propertiesGUI(Model& skeleton) override;
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
