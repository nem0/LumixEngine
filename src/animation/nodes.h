#pragma once

#include "animation/animation.h"
#include "engine/array.h"
#include "engine/math.h"
#include "engine/stream.h"


namespace Lumix
{

struct Model;
struct Pose;

namespace anim
{

struct Controller;
struct GroupNode;

struct RuntimeContext {
	RuntimeContext(Controller& controller, IAllocator& allocator);

	void setInput(u32 input_idx, float value);
	void setInput(u32 input_idx, bool value);

	Controller& controller;
	Array<u8> inputs;
	Array<Animation*> animations;
	OutputMemoryStream data;
	OutputMemoryStream events;
	
	BoneNameHash root_bone_hash;
	Time time_delta;
	Model* model = nullptr;
	InputMemoryStream input_runtime;
};

struct Node {
	enum Type : u32 {
		ANIMATION,
		GROUP,
		BLEND1D,
		LAYERS,
		CONDITION,
		NONE,
		SELECT,
		BLEND2D
	};

	Node(Node* parent, IAllocator& allocator) 
		: m_name("", allocator)
		, m_parent(parent)
		, m_events(allocator)
	{}

	virtual ~Node() {}
	virtual Type type() const = 0;
	virtual void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const = 0;
	virtual void enter(RuntimeContext& ctx) const = 0;
	virtual void skip(RuntimeContext& ctx) const = 0;
	virtual void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const = 0;
	virtual void serialize(OutputMemoryStream& stream) const = 0;
	virtual void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) = 0;
	virtual Time length(const RuntimeContext& ctx) const = 0;
	virtual Time time(const RuntimeContext& ctx) const = 0;

	void emitEvents(Time old_time, Time new_time, Time loop_length, RuntimeContext& ctx) const;

	static Node* create(Node* parent, Type type, IAllocator& allocator);

	Node* m_parent;
	String m_name;
	OutputMemoryStream m_events;
};

struct SelectNode final : Node {
	struct RuntimeData {
		u32 from;
		u32 to;
		Time t;
	};

	SelectNode(Node* parent, IAllocator& allocator);
	~SelectNode();

	Type type() const override { return SELECT; }
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) const override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;
	u32 getChildIndex(float input_val) const;

	struct Child {
		float max_value = 0;
		Node* node = nullptr;
	};

	IAllocator& m_allocator;
	Array<Child> m_children;
	u32 m_input_index = 0;
	Time m_blend_length = Time::fromSeconds(0.3f);
};

struct AnimationNode final : Node {
	AnimationNode(Node* parent, IAllocator& allocator);
	Type type() const override { return ANIMATION; }
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) const override;
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
	Blend2DNode(Node* parent, IAllocator& allocator);
	Type type() const override { return BLEND2D; }
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) const override;
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

	Array<Child> m_children;
	Array<Triangle> m_triangles;
	u32 m_x_input_index = 0;
	u32 m_y_input_index = 0;
};

struct Blend1DNode final : Node {
	Blend1DNode(Node* parent, IAllocator& allocator);
	Type type() const override { return BLEND1D; }
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) const override;
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

	Array<Child> m_children;
	u32 m_input_index = 0;
};

struct ConditionNode final : Node {
	struct RuntimeData {
		bool is_true;
		Time t;
	};

	ConditionNode(Node* parent, IAllocator& allocator);
	~ConditionNode();
	Type type() const override { return CONDITION; }
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) const override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;


	IAllocator& m_allocator;
	Condition m_condition;
	Node* m_true_node = nullptr;
	Node* m_false_node = nullptr;
	String m_condition_str;
	Time m_blend_length = Time::fromSeconds(0.3f);
};

struct GroupNode final : Node {
	GroupNode(Node* parent, IAllocator& allocator);
	GroupNode(GroupNode&& rhs) = default;
	~GroupNode();
	Type type() const override { return GROUP; }

	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) const override;
	void skip(RuntimeContext& ctx) const override;
	void getPose(RuntimeContext& ctx, float weight, Pose& pose, u32 mask) const override;
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	struct RuntimeData {
		u32 from;
		u32 to;
		Time t;
		Time blend_length;
	};

	struct Transition {
		u32 from = 0;
		u32 to = 0;
		Time blend_length = Time::fromSeconds(0.3f);
		float exit_time = -1;
	};

	struct Child {
		enum Flags : u32 {
			SELECTABLE = 1 << 0
		};

		Child(IAllocator& allocator) 
			: condition(allocator) 
			, condition_str("", allocator)
		{}

		Condition condition;
		String condition_str;
		Node* node;
		u32 flags = SELECTABLE;
	};

	IAllocator& m_allocator;
	Time m_blend_length = Time::fromSeconds(0.3f);
	Array<Child> m_children;
	Array<Transition> m_transitions;
};

struct LayersNode final : Node {
	LayersNode(Node* parent, IAllocator& allocator);
	~LayersNode();
	Type type() const override { return LAYERS; }
	
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) const override;
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
