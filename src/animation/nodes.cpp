#include "animation.h"
#include "condition.h"
#include "controller.h"
#include "engine/log.h"
#include "nodes.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "engine/crt.h"


namespace Lumix::anim {



static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotion(const Animation* anim, Time time, int translation_idx, int rotation_idx) {
	LocalRigidTransform root_motion;
	if (translation_idx >= 0) {
		root_motion.pos = anim->getTranslation(time, translation_idx);
	}
	else {
		root_motion.pos = Vec3::ZERO;
	}
	if (rotation_idx >= 0) {
		root_motion.rot = anim->getRotation(time, rotation_idx);
	}
	else {
		root_motion.rot = Quat::IDENTITY;
	}
	return root_motion;
}

static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotionSimple(const Animation* anim, Time t0, Time t1, int translation_idx, int rotation_idx) {
	LocalRigidTransform root_motion;

	const LocalRigidTransform old_tr = getRootMotion(anim, t0, translation_idx, rotation_idx);
	const Quat old_rot_inv = old_tr.rot.conjugated();
	ASSERT(t0 <= t1);
	const LocalRigidTransform new_tr = getRootMotion(anim, t1, translation_idx, rotation_idx);
	root_motion.rot = old_rot_inv * new_tr.rot;
	root_motion.pos = old_rot_inv.rotate(new_tr.pos - old_tr.pos);
	return root_motion;
}

static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotion(const Animation* anim, Time t0, Time t1, int translation_idx, int rotation_idx) {
	if (t0 < t1) return getRootMotionSimple(anim, t0, t1, translation_idx, rotation_idx);

	const LocalRigidTransform tr_0 = getRootMotionSimple(anim, t0, anim->getLength(), translation_idx, rotation_idx);
	const LocalRigidTransform tr_1 = getRootMotionSimple(anim, Time::fromSeconds(0), t1, translation_idx, rotation_idx);
	
	LocalRigidTransform root_motion;
	root_motion.rot = tr_1.rot * tr_0.rot;
	root_motion.pos = tr_0.pos + tr_0.rot.rotate(tr_1.pos);
	return root_motion;
}

static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotion(const RuntimeContext& ctx, u32 slot, Time t0_abs, Time t1_abs) {
	Animation* anim = ctx.animations[slot];
	if (!anim) return { {0, 0, 0}, {0, 0, 0, 1} };

	// TODO getBoneIndex is O(n)
	const int translation_idx = anim->getTranslationCurveIndex(ctx.root_bone_hash);
	const int rotation_idx = anim->getRotationCurveIndex(ctx.root_bone_hash);

	const Time t0 = t0_abs % anim->getLength();
	const Time t1 = t1_abs % anim->getLength();

	if (t0 < t1) return getRootMotionSimple(anim, t0, t1, translation_idx, rotation_idx);

	const LocalRigidTransform tr_0 = getRootMotionSimple(anim, t0, anim->getLength(), translation_idx, rotation_idx);
	const LocalRigidTransform tr_1 = getRootMotionSimple(anim, Time::fromSeconds(0), t1, translation_idx, rotation_idx);
	
	LocalRigidTransform root_motion;
	root_motion.rot = tr_1.rot * tr_0.rot;
	root_motion.pos = tr_0.pos + tr_0.rot.rotate(tr_1.pos);
	return root_motion;
}

RuntimeContext::RuntimeContext(Controller& controller, IAllocator& allocator)
	: data(allocator)
	, inputs(allocator)
	, controller(controller)
	, animations(allocator)
	, input_runtime(nullptr, 0)
{
}

static u32 getInputByteOffset(Controller& controller, u32 input_idx) {
	u32 offset = 0;
	for (u32 i = 0; i < input_idx; ++i) {
		switch (controller.m_inputs.inputs[i].type) {
			case InputDecl::FLOAT: offset += sizeof(float); break;
			case InputDecl::BOOL: offset += sizeof(bool); break;
			case InputDecl::U32: offset += sizeof(u32); break;
			case InputDecl::EMPTY: break;
		}
	}
	return offset;
}

void RuntimeContext::setInput(u32 input_idx, float value) {
	ASSERT(controller.m_inputs.inputs[input_idx].type == InputDecl::FLOAT);
	const u32 offset = getInputByteOffset(controller, input_idx);
	memcpy(&inputs[offset], &value, sizeof(value));
}

void RuntimeContext::setInput(u32 input_idx, bool value) {
	ASSERT(controller.m_inputs.inputs[input_idx].type == InputDecl::BOOL);
	const u32 offset = getInputByteOffset(controller, input_idx);
	memcpy(&inputs[offset], &value, sizeof(value));
}

Blend1DNode::Blend1DNode(GroupNode* parent, IAllocator& allocator)
	: Node(parent, allocator) 
	, m_children(allocator)
{}

static float getInputValue(const RuntimeContext& ctx, u32 idx) {
	const InputDecl::Input& input = ctx.controller.m_inputs.inputs[idx];
	ASSERT(input.type == InputDecl::FLOAT);
	return *(float*)&ctx.inputs[input.offset];
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

void Blend1DNode::update(RuntimeContext& ctx, Ref<LocalRigidTransform> root_motion) const {
	Time t = ctx.input_runtime.read<Time>();
	const Time t0 = t;
	t += ctx.time_delta;
	ctx.data.write(t);
	
	const float input_val = getInputValue(ctx, m_input_index);
	const Blend1DActivePair pair = getActivePair(*this, input_val);
	
	root_motion = getRootMotion(ctx, pair.a->slot, t0, t);
	if (pair.b) {
		const LocalRigidTransform tr1 = getRootMotion(ctx, pair.b->slot, t0, t);
		root_motion = root_motion->interpolate(tr1, pair.t);
	}
}

void Blend1DNode::enter(RuntimeContext& ctx) const {
	Time t = Time::fromSeconds(0);
	ctx.data.write(t);
}

void Blend1DNode::skip(RuntimeContext& ctx) const {
	ctx.input_runtime.skip(sizeof(Time));
}

static void getPose(const RuntimeContext& ctx, Time time, float weight, u32 slot, Ref<Pose> pose, u32 mask_idx, bool looped) {
	Animation* anim = ctx.animations[slot];
	if (!anim) return;
	if (!ctx.model->isReady()) return;
	if (!anim->isReady()) return;

	const Time anim_time = looped ? time % anim->getLength() : minimum(time, anim->getLength());

	const BoneMask* mask = mask_idx < (u32)ctx.controller.m_bone_masks.size() ? &ctx.controller.m_bone_masks[mask_idx] : nullptr;
	anim->getRelativePose(anim_time, pose, *ctx.model, weight, mask);
}

void Blend1DNode::getPose(RuntimeContext& ctx, float weight, Ref<Pose> pose, u32 mask) const {
	const Time t = ctx.input_runtime.read<Time>();

	if (m_children.empty()) return;
	if (m_children.size() == 1) {
		anim::getPose(ctx, t, weight, m_children[0].slot, pose, mask, true);
		return;
	}

	const float input_val = getInputValue(ctx, m_input_index);
	const Blend1DActivePair pair = getActivePair(*this, input_val);
	
	anim::getPose(ctx, t, weight, pair.a->slot, pose, mask, true);
	if (pair.b) {
		anim::getPose(ctx, t, weight * pair.t, pair.b->slot, pose, mask, true);
	}
}

void Blend1DNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_input_index);
	stream.write((u32)m_children.size());
	stream.write(m_children.begin(), m_children.byte_size());
}

void Blend1DNode::deserialize(InputMemoryStream& stream, Controller& ctrl) {
	Node::deserialize(stream, ctrl);
	stream.read(m_input_index);
	u32 count;
	stream.read(count);
	m_children.resize(count);
	stream.read(m_children.begin(), m_children.byte_size());
}

AnimationNode::AnimationNode(GroupNode* parent, IAllocator& allocator) 
	: Node(parent, allocator) 
{}

void AnimationNode::update(RuntimeContext& ctx, Ref<LocalRigidTransform> root_motion) const {
	Time t = ctx.input_runtime.read<Time>();
	Time prev_t = t;
	t += ctx.time_delta;

	Animation* anim = ctx.animations[m_slot];
	if (anim) {
		// TODO getBoneIndex is O(n)
		
		const int translation_idx = anim->getTranslationCurveIndex(ctx.root_bone_hash);
		const int rotation_idx = anim->getRotationCurveIndex(ctx.root_bone_hash);
		if (rotation_idx >= 0 || translation_idx >= 0) {
			const Time t1 = t % anim->getLength();
			const Time t0 = prev_t % anim->getLength();
			root_motion = getRootMotion(anim, t0, t1, translation_idx, rotation_idx);
		}
	}
	else {
		root_motion = {{0, 0, 0}, {0, 0, 0, 1}};
	}
	ctx.data.write(t);
}

void AnimationNode::enter(RuntimeContext& ctx) const {
	Time t = Time::fromSeconds(0); 
	ctx.data.write(t);	
}

void AnimationNode::skip(RuntimeContext& ctx) const {
	ctx.input_runtime.skip(sizeof(Time));
}
	
void AnimationNode::getPose(RuntimeContext& ctx, float weight, Ref<Pose> pose, u32 mask) const {
	const Time t = ctx.input_runtime.read<Time>();
	anim::getPose(ctx, t, weight, m_slot, pose, mask, m_flags & LOOPED);
}

void AnimationNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_slot);
	stream.write(m_flags);
}

void AnimationNode::deserialize(InputMemoryStream& stream, Controller& ctrl) {
	Node::deserialize(stream, ctrl);
	stream.read(m_slot);
	stream.read(m_flags);
}

LayersNode::Layer::Layer(GroupNode* parent, IAllocator& allocator) 
 : node(parent, allocator)
 , name(allocator)
{
}

LayersNode::LayersNode(GroupNode* parent, IAllocator& allocator) 
	: Node(parent, allocator)
	, m_layers(allocator)
	, m_allocator(allocator)
{
}

void LayersNode::update(RuntimeContext& ctx, Ref<LocalRigidTransform> root_motion) const {
	for (const Layer& layer : m_layers) {
		LocalRigidTransform tmp_rm;
		layer.node.update(ctx, Ref(tmp_rm));
		if (&layer == m_layers.begin()) {
			root_motion = tmp_rm;
		}
	}
}

void LayersNode::enter(RuntimeContext& ctx) const {
	for (const Layer& layer : m_layers) {
		layer.node.enter(ctx);
	}
}

void LayersNode::skip(RuntimeContext& ctx) const {
	for (const Layer& layer : m_layers) {
		layer.node.skip(ctx);
	}
}

void LayersNode::getPose(RuntimeContext& ctx, float weight, Ref<Pose> pose, u32 mask) const {
	for (const Layer& layer : m_layers) {
		layer.node.getPose(ctx, weight, pose, layer.mask);
	}
}

void LayersNode::serialize(OutputMemoryStream& stream) const {
	stream.write((u32)m_layers.size());
	for (const Layer& layer : m_layers) {
		stream.writeString(layer.name.c_str());
		stream.write(layer.mask);
		layer.node.serialize(stream);
	}
}

void LayersNode::deserialize(InputMemoryStream& stream, Controller& ctrl) {
	u32 c;
	stream.read(c);
	for (u32 i = 0; i < c; ++i) {
		Layer& layer = m_layers.emplace(m_parent, m_allocator);
		layer.name = stream.readString();
		stream.read(layer.mask);
		layer.node.deserialize(stream, ctrl);
	}
}

GroupNode::GroupNode(GroupNode* parent, IAllocator& allocator)
	: Node(parent, allocator)
	, m_allocator(allocator)
	, m_children(allocator)
{}

GroupNode::~GroupNode() {
	for (Child& c : m_children) {
		LUMIX_DELETE(m_allocator, c.node);
	}
}

void GroupNode::update(RuntimeContext& ctx, Ref<LocalRigidTransform> root_motion) const {
	RuntimeData data = ctx.input_runtime.read<RuntimeData>();
	
	if(data.from != data.to) {
		data.t += ctx.time_delta;

		if (m_blend_length < data.t) {
			// TODO root motion in data.from 
			m_children[data.from].node->skip(ctx);
			data.from = data.to;
			data.t = Time::fromSeconds(0);
			ctx.data.write(data);
			m_children[data.to].node->update(ctx, root_motion);
			return;
		}

		ctx.data.write(data);
		
		m_children[data.from].node->update(ctx, root_motion);
		LocalRigidTransform tmp;
		m_children[data.to].node->update(ctx, Ref(tmp));
		root_motion = root_motion->interpolate(tmp, data.t.seconds() / m_blend_length.seconds());
		return;
	}

	if (!m_children[data.from].condition.eval(ctx)) {
		for (const Child::Transition& transition : m_children[data.from].transitions) {
			if (!transition.condition.eval(ctx)) continue;
			
			data.to = transition.to;
			data.t = Time::fromSeconds(0);
			ctx.data.write(data);
			m_children[data.from].node->update(ctx, root_motion);
			m_children[data.to].node->enter(ctx);
			return;
		}

		for (u32 i = 0, c = m_children.size(); i < c; ++i) {
			const Child& child = m_children[i];
			if (i == data.from) continue;
			if ((child.flags & Child::SELECTABLE) == 0) continue;
			if (!child.condition.eval(ctx)) continue;


			data.to = i;
			data.t = Time::fromSeconds(0);
			ctx.data.write(data);
			m_children[data.from].node->update(ctx, root_motion);
			m_children[data.to].node->enter(ctx);
			return;
		}
	}

	data.t += ctx.time_delta;
	ctx.data.write(data);
	m_children[data.from].node->update(ctx, root_motion);
}
	
void GroupNode::enter(RuntimeContext& ctx) const {
	RuntimeData runtime_data = { 0, 0, Time::fromSeconds(0) };
	for (u32 i = 0, c = m_children.size(); i < c; ++i) {
		const Child& child = m_children[i];
		
		if (child.condition.eval(ctx)) {
			runtime_data =  { i, i, Time::fromSeconds(0) };
			break;
		}
	}
	ctx.data.write(runtime_data);
	if(runtime_data.from < (u32)m_children.size())
		m_children[runtime_data.from].node->enter(ctx);
}

void GroupNode::skip(RuntimeContext& ctx) const { 
	RuntimeData data = ctx.input_runtime.read<RuntimeData>();
	m_children[data.from].node->skip(ctx);
	if (data.from != data.to) {
		m_children[data.to].node->skip(ctx);
	}
}
	
void GroupNode::getPose(RuntimeContext& ctx, float weight, Ref<Pose> pose, u32 mask) const {
	const RuntimeData data = ctx.input_runtime.read<RuntimeData>();

	m_children[data.from].node->getPose(ctx, weight, pose, mask);
	if(data.from != data.to) {
		const float t = clamp(data.t.seconds() / m_blend_length.seconds(), 0.f, 1.f);
		m_children[data.to].node->getPose(ctx, weight * t, pose, mask);
	}
}

void GroupNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_blend_length);
	stream.write((u32)m_children.size());
	for (const Child& child : m_children) {
		stream.write(child.node->type());
		stream.writeString(child.condition_str.c_str());
		child.node->serialize(stream);
	}
}

void GroupNode::deserialize(InputMemoryStream& stream, Controller& ctrl) {
	Node::deserialize(stream, ctrl);
	stream.read(m_blend_length);
	u32 size;
	stream.read(size);
	m_children.reserve(size);
	for (u32 i = 0; i < size; ++i) {
		Node::Type type;
		stream.read(type);
		m_children.emplace(m_allocator);
		const char* tmp = stream.readString();
		m_children[i].condition_str = tmp;
		m_children[i].condition.compile(tmp, ctrl.m_inputs);
		m_children[i].node = Node::create(this, type, m_allocator);
		m_children[i].node->deserialize(stream, ctrl);
	}
}

void Node::serialize(OutputMemoryStream& stream) const {
	stream.writeString(m_name.c_str());
}

void Node::deserialize(InputMemoryStream& stream, Controller& ctrl) {
	const char* tmp = stream.readString();
 	m_name = tmp;
}

Node* Node::create(GroupNode* parent, Type type, IAllocator& allocator) {
	switch (type) {
		case Node::ANIMATION: return LUMIX_NEW(allocator, AnimationNode)(parent, allocator);
		case Node::GROUP: return LUMIX_NEW(allocator, GroupNode)(parent, allocator);
		case Node::BLEND1D: return LUMIX_NEW(allocator, Blend1DNode)(parent, allocator);
		case Node::LAYERS: return LUMIX_NEW(allocator, LayersNode)(parent, allocator);
		default: ASSERT(false); return nullptr;
	}
}


} // namespace Lumix::anim