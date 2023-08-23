#pragma once

#include "animation/animation.h"
#include "engine/array.h"
#include "engine/math.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "editor/utils.h"


namespace Lumix {

struct Model;
struct Pose;

namespace anim {

struct Controller;
struct GroupNode;

bool inputSlot(const Controller& controller, const char* str_id, u32* slot);
bool editInput(const char* label, u32* input_index, const Controller& controller);


struct RuntimeContext {
	RuntimeContext(Controller& controller, IAllocator& allocator);

	void setInput(u32 input_idx, float value);
	void setInput(u32 input_idx, bool value);

	union InputValue {
		float f;
		i32 i32;
		bool b;
	};

	Controller& controller;
	Array<InputValue> inputs;
	Array<Animation*> animations;
	OutputMemoryStream data;
	OutputMemoryStream events;
	
	BoneNameHash root_bone_hash;
	Time time_delta;
	Model* model = nullptr;
	InputMemoryStream input_runtime;
};

struct Node : NodeEditorNode {
	enum Type : u32 {
		ANIMATION,
		BLEND1D,
		LAYERS,
		NONE,
		SELECT,
		BLEND2D,
		TREE,
		OUTPUT
	};

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
	virtual void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const = 0;
	virtual void enter(RuntimeContext& ctx) = 0;
	virtual void skip(RuntimeContext& ctx) const = 0;
	virtual void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const = 0;
	virtual void serialize(OutputMemoryStream& stream) const;
	virtual void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version);
	virtual Time length(const RuntimeContext& ctx) const = 0;
	virtual Time time(const RuntimeContext& ctx) const = 0;
	virtual bool onGUI() { return false; }
	virtual bool propertiesGUI() { return false; }
	virtual bool compile() { return true; }
	
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

struct OutputNode final : Node {
	OutputNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return OUTPUT; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return false; }
	bool onGUI() override;
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;
	bool compile() override;

	Node* input = nullptr;
};

struct TreeNode final : Node {
	TreeNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return TREE; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return false; }
	bool onGUI() override;
	bool propertiesGUI() override;
	bool compile() override;

	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	String m_name;
};

struct SelectNode final : Node {
	struct RuntimeData {
		u32 from;
		u32 to;
		Time t;
	};

	SelectNode(Node* parent, Controller& controller, IAllocator& allocator);

	Type type() const override { return SELECT; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI() override;
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	bool compile() override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;
	u32 getChildIndex(float input_val) const;

	Array<float> m_max_values;
	Array<Node*> m_inputs; 
	u32 m_input_index = 0;
	Time m_blend_length = Time::fromSeconds(0.3f);
};

struct AnimationNode final : Node {
	AnimationNode(Node* parent, Controller& controller, IAllocator& allocator);
	Type type() const override { return ANIMATION; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI() override;
	bool compile() override;

	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	enum Flags : u32 {
		LOOPED = 1 << 0
	};

	u32 m_slot = 0;
	u32 m_flags = LOOPED;
};

struct Blend2DNode final : Node {
	Blend2DNode(Node* parent, Controller& controller, IAllocator& allocator);
	Type type() const override { return BLEND2D; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI() override;
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;
	
	void dataChanged(IAllocator& tmp_allocator);

	struct Child {
		Vec2 value = Vec2(0);
		u32 slot = 0;
	};

	struct Triangle {
		u32 a, b, c;
		Vec2 circumcircle_center;
	};

	String m_name;
	Array<Child> m_children;
	Array<Triangle> m_triangles;
	u32 m_x_input_index = 0;
	u32 m_y_input_index = 0;
	i32 m_hovered_blend2d_child = -1;
};

struct Blend1DNode final : Node {
	Blend1DNode(Node* parent, Controller& controller, IAllocator& allocator);
	Type type() const override { return BLEND1D; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	bool onGUI() override;
	bool propertiesGUI() override;
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	struct Child {
		float value = 0;
		u32 slot = 0;
	};

	String m_name;
	Array<Child> m_children;
	u32 m_input_index = 0;
};

struct LayersNode final : Node {
	LayersNode(Node* parent, Controller& controller, IAllocator& allocator);
	~LayersNode();
	Type type() const override { return LAYERS; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	struct Layer {
		Layer(IAllocator& allocator);

		Node* node = nullptr;
		u32 mask = 0;
		String name;
	};

	IAllocator& m_allocator;
	Array<Layer> m_layers;
};

} // namespace anim
} // namespace Lumix
