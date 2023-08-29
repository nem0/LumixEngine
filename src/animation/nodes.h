#pragma once

#include "animation/animation.h"
#include "engine/array.h"
#include "engine/math.h"
#include "engine/stream.h"
#include "engine/string.h"

// Runtime part of animation nodes
// For editor part of animation nodes see editor_nodes.h

namespace Lumix {

struct Model;
struct Pose;

namespace anim {

struct Controller;

enum class NodeType : u32 {
	ANIMATION,
	BLEND1D,
	LAYERS,
	NONE,
	SELECT,
	BLEND2D,
	TREE,
	OUTPUT,
	INPUT,
	SWITCH,
	CMP_EQ,
	CMP_NEQ,
	CMP_LT,
	CMP_GT,
	CMP_LTE,
	CMP_GTE,
	MUL,
	DIV,
	ADD,
	SUB,
	CONSTANT,
	AND,
	OR,
	PLAYRATE,
	IK
};

struct Node {
	static Node* create(NodeType type, Controller& controller);

	virtual ~Node() {}
	virtual NodeType type() const = 0;
	virtual void serialize(OutputMemoryStream& stream) const = 0;
	virtual void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) = 0;
};

void serializeNode(OutputMemoryStream& blob, const Node& node);
Node* deserializeNode(InputMemoryStream& blob, Controller& ctrl, u32 version);

struct PoseNode : Node {
	virtual void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const = 0;
	virtual void enter(RuntimeContext& ctx) = 0;
	virtual void skip(RuntimeContext& ctx) const = 0;
	virtual Time length(const RuntimeContext& ctx) const = 0;
	virtual Time time(const RuntimeContext& ctx) const = 0;
};

struct ValueNode : Node {
	virtual Value eval(const RuntimeContext& ctx) const = 0;
};

struct InputNode final : ValueNode {
	NodeType type() const override { return anim::NodeType::INPUT; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Value eval(const RuntimeContext& ctx) const override;

	u32 m_input_index;
};

struct ConstNode final : ValueNode {
	NodeType type() const override { return anim::NodeType::CONSTANT; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Value eval(const RuntimeContext& ctx) const override;

	Value m_value;
};

template <NodeType T>
struct MathNode final : ValueNode {
	NodeType type() const override { return T; }

	void serialize(OutputMemoryStream& stream) const override {
		serializeNode(stream, *m_input0);
		serializeNode(stream, *m_input1);
	}

	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override {
		m_input0 = (ValueNode*)deserializeNode(stream, ctrl, version);
		m_input1 = (ValueNode*)deserializeNode(stream, ctrl, version);
	}

	Value eval(const RuntimeContext& ctx) const override {
		Value v0 = m_input0->eval(ctx);
		Value v1 = m_input1->eval(ctx);
		// TODO other types
		if constexpr (T == NodeType::CMP_GT) return v0.f > v1.f;
		else if constexpr (T == NodeType::CMP_GTE) return v0.f >= v1.f;
		else if constexpr (T == NodeType::CMP_LT) return v0.f < v1.f;
		else if constexpr (T == NodeType::CMP_LTE) return v0.f <= v1.f;
		else if constexpr (T == NodeType::CMP_NEQ) return v0.f != v1.f;
		else if constexpr (T == NodeType::CMP_EQ) return v0.f == v1.f;
		
		else if constexpr (T == NodeType::AND) return v0.b && v1.b;
		else if constexpr (T == NodeType::OR) return v0.b || v1.b;

		else if constexpr (T == NodeType::MUL) return v0.f * v1.f;
		else if constexpr (T == NodeType::DIV) return v0.f / v1.f;
		else if constexpr (T == NodeType::ADD) return v0.f + v1.f;
		else if constexpr (T == NodeType::SUB) return v0.f - v1.f;
		else {
			ASSERT(false);
			return 0;
		}
	}

	ValueNode* m_input0 = nullptr;
	ValueNode* m_input1 = nullptr;
};

struct PlayRateNode final : PoseNode {
	PlayRateNode(IAllocator& allocator);
	~PlayRateNode();
	NodeType type() const override { return anim::NodeType::PLAYRATE; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	IAllocator& m_allocator;
	ValueNode* m_value = nullptr;
	PoseNode* m_node = nullptr;
};

struct Blend1DNode final : PoseNode {
	Blend1DNode(IAllocator& allocator);
	~Blend1DNode();
	NodeType type() const override { return anim::NodeType::BLEND1D; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	struct Child {
		float value = 0;
		u32 slot = 0;
	};

	IAllocator& m_allocator;
	Array<Child> m_children;
	ValueNode* m_value = nullptr;
};


struct Blend2DNode final : PoseNode {
	Blend2DNode(IAllocator& allocator);
	~Blend2DNode();
	NodeType type() const override { return anim::NodeType::BLEND2D; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	struct Child {
		Vec2 value = Vec2(0);
		u32 slot = 0;
	};

	struct Triangle {
		u32 a, b, c;
		Vec2 circumcircle_center;
	};

	IAllocator& m_allocator;
	Array<Triangle> m_triangles;
	Array<Child> m_children;
	ValueNode* m_x_value = nullptr;
	ValueNode* m_y_value = nullptr;
};

struct SelectNode final : PoseNode {
	struct RuntimeData {
		u32 from;
		u32 to;
		Time t;
	};

	SelectNode(IAllocator& allocator);
	~SelectNode();
	NodeType type() const override { return anim::NodeType::SELECT; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	IAllocator& m_allocator;
	Array<PoseNode*> m_children;
	ValueNode* m_value = nullptr;
	Time m_blend_length;
};

struct SwitchNode final : PoseNode {
	struct RuntimeData {
		bool current;
		bool switching;
		Time t;
	};

	SwitchNode(IAllocator& allocator);
	~SwitchNode();
	NodeType type() const override { return anim::NodeType::SWITCH; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	IAllocator& m_allocator;
	PoseNode* m_true_node = nullptr;
	PoseNode* m_false_node = nullptr;
	ValueNode* m_value = nullptr;
	Time m_blend_length;
};

struct IKNode final : PoseNode {
	IKNode(IAllocator& allocator);
	~IKNode();
	NodeType type() const override { return anim::NodeType::IK; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	IAllocator& m_allocator;
	ValueNode* m_alpha = nullptr;
	ValueNode* m_effector_position = nullptr;
	PoseNode* m_input = nullptr;
	u32 m_leaf_bone;
	u32 m_bones_count;
};

struct AnimationNode final : PoseNode {
	NodeType type() const override { return anim::NodeType::ANIMATION; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const override;
	void enter(RuntimeContext& ctx) override;
	void skip(RuntimeContext& ctx) const override;
	Time length(const RuntimeContext& ctx) const override;
	Time time(const RuntimeContext& ctx) const override;

	enum Flags : u32 {
		LOOPED = 1 << 0
	};

	u32 m_slot = 0;
	u32 m_flags = LOOPED;
};


} // namespace anim
} // namespace Lumix
