#pragma once

#include "animation/animation.h"
#include "engine/array.h"
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
	
	StableHash32 root_bone_hash;
	Time time_delta;
	Model* model = nullptr;
	InputMemoryStream input_runtime;
};

struct Node {
	enum Type : u32 {
		ANIMATION,
		GROUP,
		BLEND1D,
		LAYERS
	};

	Node(GroupNode* parent, IAllocator& allocator) 
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

	static Node* create(GroupNode* parent, Type type, IAllocator& allocator);

	GroupNode* m_parent;
	String m_name;
	OutputMemoryStream m_events;
};

struct AnimationNode final : Node {
	AnimationNode(GroupNode* parent, IAllocator& allocator);
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

	u32 m_slot;
	u32 m_flags = LOOPED;
};

struct Blend1DNode final : Node {
	Blend1DNode(GroupNode* parent, IAllocator& allocator);
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

struct GroupNode final : Node {
	GroupNode(GroupNode* parent, IAllocator& allocator);
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
	LayersNode(GroupNode* parent, IAllocator& allocator);
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
		Layer(GroupNode* parent, IAllocator& allocator);

		GroupNode node;
		u32 mask = 0;
		String name;
	};

	IAllocator& m_allocator;
	Array<Layer> m_layers;
};

} // namespace anim
} // namespace Lumix
