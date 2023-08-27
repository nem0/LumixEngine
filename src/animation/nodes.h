#pragma once

#include "animation/animation.h"
#include "engine/array.h"
#include "engine/math.h"
#include "engine/stream.h"
#include "engine/string.h"


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
	INPUT
};

struct Node {
	static Node* create(NodeType type, Controller& controller);

	virtual ~Node() {}
	virtual NodeType type() const = 0;
	virtual void serialize(OutputMemoryStream& stream) const = 0;
	virtual void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) = 0;
};

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

struct InputNode : ValueNode {
	NodeType type() const override { return anim::NodeType::INPUT; }
	void serialize(OutputMemoryStream& stream) const override;
	void deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) override;
	Value eval(const RuntimeContext& ctx) const override;

	u32 m_input_index;
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
	ValueNode* m_value;
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
	ValueNode* m_x_value;
	ValueNode* m_y_value;
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
	ValueNode* m_value;
	Time m_blend_length;
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
