#include "animation.h"
#include "condition.h"
#include "controller.h"
#include "nodes.h"
#include "engine/crc32.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include <math.h> // fmodf
#include <string.h> // memcpy


namespace Lumix::Anim {

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
			case InputDecl::FLOAT: offset += sizeof(float);
			case InputDecl::BOOL: offset += sizeof(bool);
			case InputDecl::U32: offset += sizeof(u32);
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

AnimationNode::AnimationNode(GroupNode* parent, IAllocator& allocator) 
	: Node(parent, allocator) 
{}

static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotionSimple(Animation* anim, float t0, float t1, u32 root_bone_idx) {
	LocalRigidTransform root_motion;

	const LocalRigidTransform old_tr = anim->getBoneTransform(t0, root_bone_idx);
	const Quat old_rot_inv = old_tr.rot.conjugated();
	ASSERT(t0 < t1);
	const LocalRigidTransform new_tr = anim->getBoneTransform(t1, root_bone_idx);
	root_motion.rot = old_rot_inv * new_tr.rot;
	root_motion.pos = old_rot_inv.rotate(new_tr.pos - old_tr.pos);
	return root_motion;
}

static LUMIX_FORCE_INLINE LocalRigidTransform getRootMotion(Animation* anim, float t0, float t1, u32 root_bone_idx) {

	if (t0 < t1) return getRootMotionSimple(anim, t0, t1, root_bone_idx);

	const LocalRigidTransform tr_0 = getRootMotionSimple(anim, t0, anim->getLength(), root_bone_idx);
	const LocalRigidTransform tr_1 = getRootMotionSimple(anim, 0, t1, root_bone_idx);
	
	LocalRigidTransform root_motion;
	root_motion.rot = tr_1.rot * tr_0.rot;
	root_motion.pos = tr_0.pos + tr_0.rot.rotate(tr_1.pos);
	return root_motion;
}

void AnimationNode::update(RuntimeContext& ctx, Ref<LocalRigidTransform> root_motion) const {
	float t = ctx.input_runtime.read<float>();

	t += ctx.time_delta;
	auto iter = ctx.animations.find(m_animation_hash);
	if(iter.isValid()) {
		Animation* anim = iter.value();
		// TODO getBoneIndex is O(n)
		const int root_bone_idx = anim->getBoneIndex(ctx.root_bone_hash);
		if (root_bone_idx >= 0) {
			const float t1 = fmodf(t, anim->getLength());
			const float t0 = fmodf(t - ctx.time_delta, anim->getLength());
			root_motion = getRootMotion(anim, t0, t1, root_bone_idx);
		}
	}
	else {
		root_motion = {{0, 0, 0}, {0, 0, 0, 1}};
	}
	ctx.data.write(t);
}

void AnimationNode::enter(RuntimeContext& ctx) const {
	float t = 0; 
	ctx.data.write(t);	
}

void AnimationNode::skip(RuntimeContext& ctx) const {
	ctx.input_runtime.skip(sizeof(float));
}
	
void AnimationNode::getPose(RuntimeContext& ctx, float weight, Pose& pose) const {
	const float t = ctx.input_runtime.read<float>();

	auto iter = ctx.animations.find(m_animation_hash);
	if (!iter.isValid()) return;

	Animation* anim = iter.value();

	const float anim_time = fmodf(t, anim->getLength());
	anim->getRelativePose(anim_time, pose, *ctx.model, weight, nullptr);
}

void AnimationNode::serialize(OutputMemoryStream& stream) const {
	Node::serialize(stream);
	stream.write(m_animation_hash);
}

void AnimationNode::deserialize(InputMemoryStream& stream, Controller& ctrl) {
	Node::deserialize(stream, ctrl);
	stream.read(m_animation_hash);
}

GroupNode::GroupNode(GroupNode* parent, IAllocator& allocator)
	: Node(parent, allocator)
	, m_allocator(allocator)
	, m_children(allocator)
{}

void GroupNode::update(RuntimeContext& ctx, Ref<LocalRigidTransform> root_motion) const {
	RuntimeData data = ctx.input_runtime.read<RuntimeData>();
	
	if(data.from != data.to) {
		data.t += ctx.time_delta;

		if (data.t > m_blend_length) {
			m_children[data.from].node->skip(ctx);
			data.from = data.to;
			data.t = 0;
			ctx.data.write(data);
			m_children[data.to].node->update(ctx, root_motion);
			return;
		}

		ctx.data.write(data);
		m_children[data.from].node->update(ctx, root_motion);
		m_children[data.to].node->update(ctx, root_motion);
		return;
	}

	for (u32 i = 0, c = m_children.size(); i < c; ++i) {
		const Child& child = m_children[i];
		if (i != data.from && child.condition.eval(ctx)) {
			data.to = i;
			data.t = 0;
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
	RuntimeData runtime_data = { 0, 0, 0 };
	for (u32 i = 0, c = m_children.size(); i < c; ++i) {
		const Child& child = m_children[i];
		
		if (child.condition.eval(ctx)) {
			runtime_data =  { i, i, 0.f };
			break;
		}
	}
	ctx.data.write(runtime_data);
	if(runtime_data.from < (u32)m_children.size())
		m_children[runtime_data.from].node->enter(ctx);
}

void GroupNode::skip(RuntimeContext& ctx) const { 
	ctx.input_runtime.skip(sizeof(RuntimeData));
}
	
void GroupNode::getPose(RuntimeContext& ctx, float weight, Pose& pose) const {
	const RuntimeData data = ctx.input_runtime.read<RuntimeData>();

	m_children[data.from].node->getPose(ctx, weight, pose);
	if(data.from != data.to) {
		const float t = clamp(data.t / m_blend_length, 0.f, 1.f);
		m_children[data.to].node->getPose(ctx, weight * t, pose);
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
		char tmp[256];
		stream.readString(tmp, sizeof(tmp));
		m_children[i].condition_str = tmp;
		m_children[i].condition.compile(tmp, ctrl.m_inputs);
		m_children[i].node = Node::create(this, type, m_allocator);
		m_children[i].node->deserialize(stream, ctrl);
	}
}

Node* Node::create(GroupNode* parent, Type type, IAllocator& allocator) {
	switch (type) {
		case Node::ANIMATION: return LUMIX_NEW(allocator, AnimationNode)(parent, allocator);
		case Node::GROUP: return LUMIX_NEW(allocator, GroupNode)(parent, allocator);
		default: ASSERT(false); return nullptr;
	}
}

const ResourceType Controller::TYPE = ResourceType("anim_controller");

Controller::Controller(const Path& path, ResourceManager& resource_manager, IAllocator& allocator) 
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_animation_slots(allocator)
	, m_animation_entries(allocator)
{}

void Controller::unload() {
	LUMIX_DELETE(m_allocator, m_root);
	m_root = nullptr;
}

void Controller::initEmpty() {
	ASSERT(!m_root);
	m_root = LUMIX_NEW(m_allocator, GroupNode)(nullptr, m_allocator);
}

bool Controller::load(u64 size, const u8* mem) {
	InputMemoryStream str(mem, size);
	deserialize(str);
	return true;
}

static u32 computeInputsSize(Controller& ctrl) {
	u32 size = 0;
	for (u32 i = 0; i < ctrl.m_inputs.inputs_count; ++i) {
		switch (ctrl.m_inputs.inputs[i].type) {
			case InputDecl::FLOAT: size += sizeof(float); break;
			case InputDecl::BOOL: size += sizeof(bool); break;
			case InputDecl::U32: size += sizeof(u32); break;
			default: ASSERT(false); break;
		}
	}
	return size;
}
	
void Controller::destroyRuntime(RuntimeContext& ctx) {
	LUMIX_DELETE(m_allocator, &ctx);
}

RuntimeContext* Controller::createRuntime(u32 anim_set) {
	RuntimeContext* ctx = LUMIX_NEW(m_allocator, RuntimeContext)(*this, m_allocator);
	ctx->inputs.resize(computeInputsSize(*this));
	for (AnimationEntry& anim : m_animation_entries) {
		if (anim.set == anim_set) {
			ctx->animations.insert(anim.slot_hash, anim.animation);
		}
	}
	m_root->enter(*ctx);
	return ctx;
}

void Controller::update(RuntimeContext& ctx, Ref<LocalRigidTransform> root_motion) {
	ASSERT(&ctx.controller == this);
	// TODO better allocation strategy
	const Span<u8> mem = ctx.data.release_ownership();
	ctx.data.reserve(mem.length());
	ctx.input_runtime.set(mem.begin(), mem.length());
	m_root->update(ctx, root_motion);
	m_allocator.deallocate(mem.begin());
	
	auto root_bone_iter = ctx.model->getBoneIndex(ctx.root_bone_hash);
	if (root_bone_iter.isValid()) {
		const int root_bone_idx = root_bone_iter.value();
		const Model::Bone& bone = ctx.model->getBone(root_bone_idx);
		LocalRigidTransform root_tr;
		root_tr = bone.transform;
		LocalRigidTransform rt;
		root_motion->rot = root_tr.rot * root_motion->rot * root_tr.rot.conjugated();
		root_motion->pos = root_tr.rot.rotate(root_motion->pos);
	}
}

void Controller::getPose(RuntimeContext& ctx, struct Pose& pose) {
	ASSERT(&ctx.controller == this);
	ctx.input_runtime.set(ctx.data.getData(), ctx.data.getPos());
	
	LocalRigidTransform root_bind_pose;
	auto root_bone_iter = ctx.model->getBoneIndex(ctx.root_bone_hash);
	if (root_bone_iter.isValid()) {
		const int root_bone_idx = root_bone_iter.value();
		root_bind_pose.pos = pose.positions[root_bone_idx];
		root_bind_pose.rot = pose.rotations[root_bone_idx];
	}
	
	m_root->getPose(ctx, 1.f, pose);
	
	// TODO this should be in AnimationNode
	if (root_bone_iter.isValid()) {
		const int root_bone_idx = root_bone_iter.value();
		pose.positions[root_bone_idx] = root_bind_pose.pos;
		pose.rotations[root_bone_idx] = root_bind_pose.rot;
	}
}

struct Header {
	enum class Version : u32 {
		LATEST
	};

	u32 magic = MAGIC;
	u32 version = (u32)Version::LATEST;

	static constexpr u32 MAGIC = '_LAC';
};

void Controller::serialize(OutputMemoryStream& stream) {
	Header header;
	stream.write(header);
	stream.write(m_flags);
	for (const InputDecl::Input& input : m_inputs.inputs) {
		if (input.type == InputDecl::Type::EMPTY) continue;
		stream.write(input.type);
		stream.write(input.name);
	}
	stream.write(InputDecl::EMPTY);
	stream.write((u32)m_animation_slots.size());
	for (const String& slot : m_animation_slots) {
		stream.writeString(slot.c_str());
	}
	stream.write((u32)m_animation_entries.size());
	for (const AnimationEntry& entry : m_animation_entries) {
		stream.write(entry.slot_hash);
		stream.write(entry.set);
		stream.writeString(entry.animation ? entry.animation->getPath().c_str() : "");
	}
	m_root->serialize(stream);
}

bool Controller::deserialize(InputMemoryStream& stream) {
	Header header;
	stream.read(header);
	stream.read(m_flags);
	InputDecl::Type type;
	stream.read(type);
	while (type != InputDecl::EMPTY) {
		const int idx = m_inputs.addInput();
		m_inputs.inputs[idx].type = type;
		stream.read(m_inputs.inputs[idx].name);
		stream.read(type);
	}
	m_inputs.recalculateOffsets();
	if (header.magic != Header::MAGIC) {
		logError("Animation") << "Invalid animation controller file " << getPath();
		return false;
	}
	if (header.version > (u32)Header::Version::LATEST) {
		logError("Animation") << "Version of animation controller " << getPath() << " is not supported";
		return false;
	}
	initEmpty();
	const u32 slots_count = stream.read<u32>();
	m_animation_slots.reserve(slots_count);
	for (u32 i = 0; i < slots_count; ++i) {
		String& slot = m_animation_slots.emplace(m_allocator);
		char tmp[64];
		stream.readString(tmp, sizeof(tmp));
		slot = tmp;
	}

	const u32 entries_count = stream.read<u32>();
	m_animation_entries.reserve(entries_count);
	for (u32 i = 0; i < entries_count; ++i) {
		AnimationEntry& entry = m_animation_entries.emplace();
		stream.read(entry.slot_hash);
		stream.read(entry.set);
		char path[MAX_PATH_LENGTH];
		stream.readString(path, sizeof(path));
		entry.animation = path[0] ? m_resource_manager.getOwner().load<Animation>(Path(path)) : nullptr;
	}


	m_root->deserialize(stream, *this);
	return true;
}


} // ns Lumix::Anim