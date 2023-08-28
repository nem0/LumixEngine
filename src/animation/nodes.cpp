#include "controller.h"
#include "nodes.h"
#include "renderer/model.h"

namespace Lumix::anim {

void serializeNode(OutputMemoryStream& blob, const Node& node) {
	blob.write(node.type());
	node.serialize(blob);
}

Node* deserializeNode(InputMemoryStream& blob, Controller& ctrl, u32 version) {
	NodeType type = blob.read<NodeType>();
	Node* n = Node::create(type, ctrl);
	n->deserialize(blob, ctrl, version);
	return n;
}

static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotionEx(const Animation* anim, Time t0, Time t1) {
	ASSERT(t0 <= t1);
	LocalRigidTransform old_tr = anim->getRootMotion(t0).inverted();
	LocalRigidTransform new_tr = anim->getRootMotion(t1);
	return old_tr * new_tr;
}

static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotion(const RuntimeContext& ctx, const Animation* anim, Time t0_abs, Time t1_abs) {
	const Time t0 = t0_abs % anim->getLength();
	const Time t1 = t1_abs % anim->getLength();

	if (t0 <= t1) return getRootMotionEx(anim, t0, t1);

	const LocalRigidTransform tr_0 = getRootMotionEx(anim, t0, anim->getLength());
	const LocalRigidTransform tr_1 = getRootMotionEx(anim, Time(0), t1);

	return tr_0 * tr_1;
}

RuntimeContext::RuntimeContext(Controller& controller, IAllocator& allocator)
	: data(allocator)
	, inputs(allocator)
	, controller(controller)
	, animations(allocator)
	, blendstack(allocator)
	, input_runtime(nullptr, 0)
{
}

void RuntimeContext::setInput(u32 input_idx, float value) {
	ASSERT(controller.m_inputs[input_idx].type == Value::FLOAT);
	inputs[input_idx].f = value;
}

void RuntimeContext::setInput(u32 input_idx, bool value) {
	ASSERT(controller.m_inputs[input_idx].type == Value::BOOL);
	inputs[input_idx].b = value;
}

Node* Node::create(NodeType type, Controller& controller) {
	switch(type) {
		case NodeType::LAYERS: ASSERT(false); return nullptr;
		case NodeType::BLEND1D: return LUMIX_NEW(controller.m_allocator, Blend1DNode)(controller.m_allocator);
		case NodeType::BLEND2D: return LUMIX_NEW(controller.m_allocator, Blend2DNode)(controller.m_allocator);
		case NodeType::SELECT: return LUMIX_NEW(controller.m_allocator, SelectNode)(controller.m_allocator);
		case NodeType::INPUT: return LUMIX_NEW(controller.m_allocator, InputNode);
		case NodeType::CONSTANT: return LUMIX_NEW(controller.m_allocator, ConstNode);
		case NodeType::ANIMATION: return LUMIX_NEW(controller.m_allocator, AnimationNode);
		case NodeType::SWITCH: return LUMIX_NEW(controller.m_allocator, SwitchNode)(controller.m_allocator);
		case NodeType::CMP_EQ: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::CMP_EQ>);
		case NodeType::CMP_NEQ: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::CMP_NEQ>);
		case NodeType::CMP_LT: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::CMP_LT>);
		case NodeType::CMP_LTE: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::CMP_LTE>);
		case NodeType::CMP_GT: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::CMP_GT>);
		case NodeType::CMP_GTE: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::CMP_GTE>);
		case NodeType::AND: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::AND>);
		case NodeType::OR: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::OR>);
		case NodeType::MUL: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::MUL>);
		case NodeType::DIV: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::DIV>);
		case NodeType::ADD: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::ADD>);
		case NodeType::SUB: return LUMIX_NEW(controller.m_allocator, MathNode<NodeType::SUB>);
		case NodeType::OUTPUT:
		case NodeType::NONE:
		case NodeType::TREE: return nullptr; // editor only node
	}
	ASSERT(false);
	return nullptr;
}

void AnimationNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	Time t = ctx.input_runtime.read<Time>();
	Time prev_t = t;
	t += ctx.time_delta;

	Animation* anim = ctx.animations[m_slot];
	if (anim && anim->isReady()) {
		if ((m_flags & LOOPED) == 0) {
			const u32 len = anim->getLength().raw();
			t = Time(minimum(t.raw(), len));
			prev_t = Time(minimum(prev_t.raw(), len));
		}

		root_motion = getRootMotion(ctx, anim, prev_t, t);
	}
	else {
		root_motion = {{0, 0, 0}, {0, 0, 0, 1}};
	}
	ctx.data.write(t);

	ctx.blendstack.write(BlendStackInstructions::SAMPLE);
	ctx.blendstack.write(m_slot);
	ctx.blendstack.write(ctx.weight);
	ctx.blendstack.write(t);
	ctx.blendstack.write((m_flags & LOOPED) != 0);
}

Time AnimationNode::length(const RuntimeContext& ctx) const {
	Animation* anim = ctx.animations[m_slot];
	if (!anim) return Time(0);
	return anim->getLength();
}

Time AnimationNode::time(const RuntimeContext& ctx) const {
	return ctx.input_runtime.getAs<Time>();
}

void AnimationNode::enter(RuntimeContext& ctx) {
	Time t = Time(0); 
	ctx.data.write(t);	
}

void AnimationNode::skip(RuntimeContext& ctx) const {
	ctx.input_runtime.skip(sizeof(Time));
}

void AnimationNode::serialize(OutputMemoryStream& stream) const {
	stream.write(m_slot);
	stream.write(m_flags);
}

void AnimationNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	stream.read(m_slot);
	stream.read(m_flags);
}

SelectNode::~SelectNode() {
	LUMIX_DELETE(m_allocator, m_value);
}

SelectNode::SelectNode(IAllocator& allocator)
	: m_children(allocator)
	, m_allocator(allocator)
{}

void SelectNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	RuntimeData data = ctx.input_runtime.read<RuntimeData>();
	
	i32 child_idx = m_value->eval(ctx).toI32();
	child_idx = clamp(child_idx, 0, m_children.size() - 1);

	if (data.from != data.to) {
		data.t += ctx.time_delta;

		if (data.t > m_blend_length) {
			// TODO root motion in data.from
			m_children[data.from]->skip(ctx);
			data.from = data.to;
			data.t = Time(0);
			ctx.data.write(data);
			m_children[data.to]->update(ctx, root_motion);
			return;
		}

		ctx.data.write(data);

		m_children[data.from]->update(ctx, root_motion);
		
		const float t = clamp(data.t.seconds() / m_blend_length.seconds(), 0.f, 1.f);
		const float old_w = ctx.weight;
		ctx.weight *= t;
		LocalRigidTransform tmp;
		m_children[data.to]->update(ctx, tmp);
		ctx.weight = old_w;

		root_motion = root_motion.interpolate(tmp, data.t.seconds() / m_blend_length.seconds());
		return;
	}

	if (child_idx != data.from) {
		data.to = child_idx;
		data.t = Time(0);
		ctx.data.write(data);
		m_children[data.from]->update(ctx, root_motion);
		m_children[data.to]->enter(ctx);
		return;
	}

	data.t += ctx.time_delta;
	ctx.data.write(data);
	m_children[data.from]->update(ctx, root_motion);
}

void SelectNode::enter(RuntimeContext& ctx) {
	RuntimeData runtime_data = { 0, 0, Time(0) };
	i32 child_idx = m_value->eval(ctx).toI32();
	child_idx = clamp(child_idx, 0, m_children.size() - 1);
	runtime_data.from = child_idx;
	runtime_data.to = runtime_data.from;
	ctx.data.write(runtime_data);
	if (runtime_data.from < (u32)m_children.size()) {
		m_children[runtime_data.from]->enter(ctx);
	}
}

void SelectNode::skip(RuntimeContext& ctx) const {
	RuntimeData data = ctx.input_runtime.read<RuntimeData>();
	m_children[data.from]->skip(ctx);
	if (data.from != data.to) {
		m_children[data.to]->skip(ctx);
	}
}

void SelectNode::serialize(OutputMemoryStream& stream) const {
	stream.write(m_blend_length);
	stream.write(m_children.size());
	for (const Node* n : m_children) {
		serializeNode(stream, *n);
	}
	serializeNode(stream, *m_value);
}

void SelectNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	stream.read(m_blend_length);
	const u32 count = stream.read<u32>();
	m_children.resize(count);
	for (u32 i = 0; i < count; ++i) {
		m_children[i] = (PoseNode*)deserializeNode(stream, ctrl, version);
	}
	m_value = (ValueNode*)deserializeNode(stream, ctrl, version);
}

Time SelectNode::length(const RuntimeContext& ctx) const {	return Time::fromSeconds(1); }

Time SelectNode::time(const RuntimeContext& ctx) const { return Time(0); }


SwitchNode::~SwitchNode() {
	LUMIX_DELETE(m_allocator, m_value);
	LUMIX_DELETE(m_allocator, m_true_node);
	LUMIX_DELETE(m_allocator, m_false_node);
}

SwitchNode::SwitchNode(IAllocator& allocator)
	: m_allocator(allocator)
{}

void SwitchNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	RuntimeData data = ctx.input_runtime.read<RuntimeData>();
	
	bool condition = m_value->eval(ctx).toBool();

	if (data.switching) {
		data.t += ctx.time_delta;

		if (data.t > m_blend_length) {
			// TODO root motion in data.from
			(data.current ? m_false_node : m_true_node)->skip(ctx);
			data.switching = false;
			data.t = Time(0);
			ctx.data.write(data);
			(data.current ? m_true_node : m_false_node)->update(ctx, root_motion);
			return;
		}

		ctx.data.write(data);

		(data.current ? m_false_node : m_true_node)->update(ctx, root_motion);
		
		const float t = clamp(data.t.seconds() / m_blend_length.seconds(), 0.f, 1.f);
		const float old_w = ctx.weight;
		ctx.weight *= t;
		LocalRigidTransform tmp;
		(data.current ? m_true_node : m_false_node)->update(ctx, tmp);
		ctx.weight = old_w;

		root_motion = root_motion.interpolate(tmp, data.t.seconds() / m_blend_length.seconds());
		return;
	}

	if (data.current != condition) {
		data.switching = true;
		data.current = condition;
		data.t = Time(0);
		ctx.data.write(data);
		(data.current ? m_false_node : m_true_node)->update(ctx, root_motion);
		(data.current ? m_true_node : m_false_node)->enter(ctx);
		return;
	}

	data.t += ctx.time_delta;
	ctx.data.write(data);
	(data.current ? m_true_node : m_false_node)->update(ctx, root_motion);
}

void SwitchNode::enter(RuntimeContext& ctx) {
	RuntimeData runtime_data;
	bool condition = m_value->eval(ctx).toBool();
	runtime_data.current = condition;
	runtime_data.switching = false;
	runtime_data.t = Time(0);
	ctx.data.write(runtime_data);
	(runtime_data.current ? m_true_node : m_false_node)->enter(ctx);
}

void SwitchNode::skip(RuntimeContext& ctx) const {
	RuntimeData data = ctx.input_runtime.read<RuntimeData>();
	if (data.switching) {
		(data.current ? m_false_node : m_true_node)->skip(ctx);
	}
	(data.current ? m_true_node : m_false_node)->skip(ctx);
}

void SwitchNode::serialize(OutputMemoryStream& stream) const {
	stream.write(m_blend_length);
	serializeNode(stream, *m_true_node);
	serializeNode(stream, *m_false_node);
	serializeNode(stream, *m_value);
}

void SwitchNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	stream.read(m_blend_length);
	m_true_node = (PoseNode*)deserializeNode(stream, ctrl, version);
	m_false_node = (PoseNode*)deserializeNode(stream, ctrl, version);
	m_value = (ValueNode*)deserializeNode(stream, ctrl, version);
}

Time SwitchNode::length(const RuntimeContext& ctx) const {	return Time::fromSeconds(1); }

Time SwitchNode::time(const RuntimeContext& ctx) const { return Time(0); }

void InputNode::serialize(OutputMemoryStream& stream) const {
	stream.write(m_input_index);
}

void InputNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	stream.read(m_input_index);
}

Value InputNode::eval(const RuntimeContext& ctx) const {
	return ctx.inputs[m_input_index];
}

void ConstNode::serialize(OutputMemoryStream& stream) const {
	stream.write(m_value);
}

void ConstNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	stream.read(m_value);
}

Value ConstNode::eval(const RuntimeContext& ctx) const {
	return m_value;
}

struct Blend2DActiveTrio {
	u32 a, b, c;
	float ta, tb, tc;
};

bool getBarycentric(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c, Vec2& uv) {
  const Vec2 ab = b - a, ac = c - a, ap = p - a;

  float d00 = dot(ab, ab);
  float d01 = dot(ab, ac);
  float d11 = dot(ac, ac);
  float d20 = dot(ap, ab);
  float d21 = dot(ap, ac);
  float denom = d00 * d11 - d01 * d01;

  uv.x = (d11 * d20 - d01 * d21) / denom;
  uv.y = (d00 * d21 - d01 * d20) / denom;  
  return uv.x >= 0.f && uv.y >= 0.f && uv.x + uv.y <= 1.f;
}

static Blend2DActiveTrio getActiveTrio(const Blend2DNode& node, Vec2 input_val) {
	const Blend2DNode::Child* children = node.m_children.begin();
	Vec2 uv;
	for (const Blend2DNode::Triangle& t : node.m_triangles) {
		if (!getBarycentric(input_val, children[t.a].value, children[t.b].value, children[t.c].value, uv)) continue;
		
		Blend2DActiveTrio res;
		res.a = node.m_children[t.a].slot;
		res.b = node.m_children[t.b].slot;
		res.c = node.m_children[t.c].slot;
		res.ta = 1 - uv.x - uv.y;
		res.tb = uv.x;
		res.tc = uv.y;
		return res;
	}

	Blend2DActiveTrio res;
	res.a = node.m_children[0].slot;
	res.b = node.m_children[0].slot;
	res.c = node.m_children[0].slot;
	res.ta = 1;
	res.tb = res.tc = 0;
	return res;
}

Blend2DNode::~Blend2DNode() {
	LUMIX_DELETE(m_allocator, m_x_value);
	LUMIX_DELETE(m_allocator, m_y_value);
}

Blend2DNode::Blend2DNode(IAllocator& allocator)
	: m_children(allocator)
	, m_triangles(allocator)
	, m_allocator(allocator)
{}

void Blend2DNode::serialize(OutputMemoryStream& stream) const {
	stream.writeArray(m_children);
	stream.writeArray(m_triangles);
	serializeNode(stream, *m_x_value);
	serializeNode(stream, *m_y_value);
}

void Blend2DNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	stream.readArray(&m_children);
	stream.readArray(&m_triangles);
	m_x_value = (ValueNode*)deserializeNode(stream, ctrl, version);
	m_y_value = (ValueNode*)deserializeNode(stream, ctrl, version);
}

static Time toTime(const Animation& anim, float relt) {
	return anim.getLength() * relt;
}

void Blend2DNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	float relt = ctx.input_runtime.read<float>();
	const float relt0 = relt;
	
	Vec2 input_val;
	input_val.x = m_x_value->eval(ctx).toFloat();
	input_val.y = m_y_value->eval(ctx).toFloat();
	const Blend2DActiveTrio trio = getActiveTrio(*this, input_val);
	const Animation* anim_a = ctx.animations[trio.a];
	const Animation* anim_b = ctx.animations[trio.b];
	const Animation* anim_c = ctx.animations[trio.c];
	if (!anim_a || !anim_b || !anim_c || !anim_a->isReady() || !anim_b->isReady() || !anim_c->isReady()) {
		ctx.data.write(relt);
		return;
	}
	
	const Time wlen = anim_a->getLength() * trio.ta + anim_b->getLength() * trio.tb + anim_c->getLength() * trio.tc;
	relt += ctx.time_delta / wlen;
	relt = fmodf(relt, 1);
		
	{
		const Time len = anim_a->getLength();
		const Time t0 = len * relt0;
		const Time t = len * relt;
		root_motion = getRootMotion(ctx, anim_a, t0, t);
	}
	
	if (trio.tb > 0) {
		const Time len = anim_b->getLength();
		const Time t0 = len * relt0;
		const Time t = len * relt;
		const LocalRigidTransform tr1 = getRootMotion(ctx, anim_b, t0, t);
		root_motion = root_motion.interpolate(tr1, trio.tb / (trio.ta + trio.tb));
	}
	
	if (trio.tc > 0) {
		const Time len = anim_c->getLength();
		const Time t0 = len * relt0;
		const Time t = len * relt;
		const LocalRigidTransform tr1 = getRootMotion(ctx, anim_c, t0, t);
		root_motion = root_motion.interpolate(tr1, trio.tc);
	}

	ctx.data.write(relt);
	
	ctx.blendstack.write(BlendStackInstructions::SAMPLE);
	ctx.blendstack.write(trio.a);
	ctx.blendstack.write(ctx.weight);
	ctx.blendstack.write(toTime(*anim_a, relt));
	ctx.blendstack.write(true);
	if (trio.tb > 0) {
		ctx.blendstack.write(BlendStackInstructions::SAMPLE);
		ctx.blendstack.write(trio.b);
		ctx.blendstack.write(ctx.weight * (trio.tb / (trio.ta + trio.tb)));
		ctx.blendstack.write(toTime(*anim_b, relt));
		ctx.blendstack.write(true);
	}
	if (trio.tc > 0) {
		ctx.blendstack.write(BlendStackInstructions::SAMPLE);
		ctx.blendstack.write(trio.c);
		ctx.blendstack.write(ctx.weight * (trio.tc / (trio.ta + trio.tb + trio.tc)));
		ctx.blendstack.write(toTime(*anim_c, relt));
		ctx.blendstack.write(true);
	}
}

Time Blend2DNode::length(const RuntimeContext& ctx) const {
	if (m_children.size() < 3) return Time(1);

	Vec2 input_val;
	input_val.x = m_x_value->eval(ctx).toFloat();
	input_val.y = m_y_value->eval(ctx).toFloat();
	const Blend2DActiveTrio trio = getActiveTrio(*this, input_val);

	Animation* anim_a = ctx.animations[trio.a];
	Animation* anim_b = ctx.animations[trio.b];
	Animation* anim_c = ctx.animations[trio.c];
	if (!anim_a || !anim_a->isReady()) return Time::fromSeconds(1);
	if (!anim_b || !anim_b->isReady()) return Time::fromSeconds(1);
	if (!anim_c || !anim_c->isReady()) return Time::fromSeconds(1);
	
	return anim_a->getLength() * trio.ta + anim_b->getLength() * trio.tb + anim_c->getLength() * trio.tc;
}

void Blend2DNode::enter(RuntimeContext& ctx) {
	const float t = 0.f;
	ctx.data.write(t);
}

void Blend2DNode::skip(RuntimeContext& ctx) const {
	ctx.input_runtime.skip(sizeof(float));
}

Time Blend2DNode::time(const RuntimeContext& ctx) const {
	return length(ctx) * ctx.input_runtime.getAs<float>();
}

struct Blend1DActivePair {
	const Blend1DNode::Child* a;
	const Blend1DNode::Child* b;
	float t;
};

static Blend1DActivePair getActivePair(const Blend1DNode& node, float input_val) {
	const auto& children = node.m_children;
	if (input_val > children[0].value) {
		if (input_val >= children.back().value) {
			return { &children.back(), nullptr, 0 };
		}
		else {
			for (u32 i = 1, c = children.size(); i < c; ++i) {
				if (input_val < children[i].value) {
					const float w = (input_val - children[i - 1].value) / (children[i].value - children[i - 1].value);
					return { &children[i - 1], &children[i], w };
				}
			}
		}
	}
	return { &children[0], nullptr, 0 };
}

Blend1DNode::~Blend1DNode() {
	LUMIX_DELETE(m_allocator, m_value);
}

Blend1DNode::Blend1DNode(IAllocator& allocator)
	: m_children(allocator)
	, m_allocator(allocator)
{}

void Blend1DNode::serialize(OutputMemoryStream& stream) const {
	stream.writeArray(m_children);
	serializeNode(stream, *m_value);
}

void Blend1DNode::deserialize(InputMemoryStream& stream, Controller& ctrl, u32 version) {
	stream.readArray(&m_children);
	m_value = (ValueNode*)deserializeNode(stream, ctrl, version);
}

void Blend1DNode::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	float relt = ctx.input_runtime.read<float>();
	const float relt0 = relt;
	
	const float input_val = m_value->eval(ctx).toFloat();
	const Blend1DActivePair pair = getActivePair(*this, input_val);
	const Animation* anim_a = pair.a ? ctx.animations[pair.a->slot] : nullptr;
	const Animation* anim_b = pair.b ? ctx.animations[pair.b->slot] : nullptr;
	const Time wlen = anim_a ? lerp(anim_a->getLength(), anim_b ? anim_b->getLength() : anim_a->getLength(), pair.t) : Time::fromSeconds(1);
	relt += ctx.time_delta / wlen;
	relt = fmodf(relt, 1);
	
	if (anim_a) {
		const Time len = anim_a->getLength();
		const Time t0 = len * relt0;
		const Time t = len * relt;
		root_motion = getRootMotion(ctx, ctx.animations[pair.a->slot], t0, t);
	}
	else {
		root_motion = {{0, 0, 0}, {0, 0, 0, 1}};
	}
	if (anim_b && anim_b->isReady()) {
		const Time len = anim_b->getLength();
		const Time t0 = len * relt0;
		const Time t = len * relt;
		const LocalRigidTransform tr1 = getRootMotion(ctx, ctx.animations[pair.b->slot], t0, t);
		root_motion = root_motion.interpolate(tr1, pair.t);
	}

	ctx.data.write(relt);
	
	ctx.blendstack.write(BlendStackInstructions::SAMPLE);
	ctx.blendstack.write(pair.a->slot);
	ctx.blendstack.write(ctx.weight);
	ctx.blendstack.write(toTime(*anim_a, relt));
	ctx.blendstack.write(true);

	if (pair.b) {
		ctx.blendstack.write(BlendStackInstructions::SAMPLE);
		ctx.blendstack.write(pair.a->slot);
		ctx.blendstack.write(ctx.weight * pair.t);
		ctx.blendstack.write(toTime(*anim_b, relt));
		ctx.blendstack.write(true);
	}
}

Time Blend1DNode::length(const RuntimeContext& ctx) const {
	const float input_val = m_value->eval(ctx).toFloat();
	const Blend1DActivePair pair = getActivePair(*this, input_val);
	Animation* anim_a = ctx.animations[pair.a->slot];
	if (!anim_a) return Time::fromSeconds(1);
	if (!anim_a->isReady()) return Time::fromSeconds(1);
	
	Animation* anim_b = pair.b ? ctx.animations[pair.b->slot] : nullptr;
	if (!anim_b) return anim_a->getLength();
	if (!anim_b->isReady()) return anim_a->getLength();

	return lerp(anim_a->getLength(), anim_b->getLength(), pair.t);
}

Time Blend1DNode::time(const RuntimeContext& ctx) const {
	return length(ctx) * ctx.input_runtime.getAs<float>();
}

void Blend1DNode::enter(RuntimeContext& ctx) {
	const float t = 0.f;
	ctx.data.write(t);
}

void Blend1DNode::skip(RuntimeContext& ctx) const {
	ctx.input_runtime.skip(sizeof(float));
}

} // namespace Lumix::anim