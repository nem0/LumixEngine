#include "animation.h"
#include "controller.h"
#include "nodes.h"
#include "engine/hash.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/stack_array.h"
#include "renderer/model.h"
#include "renderer/pose.h"


namespace Lumix::anim {

const ResourceType Controller::TYPE = ResourceType("anim_controller");

Controller::Controller(const Path& path, ResourceManager& resource_manager, IAllocator& allocator) 
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_animation_entries(allocator)
	, m_inputs(allocator)
	, m_ik(allocator)
	, m_bone_masks(allocator)
{}

Controller::~Controller() {
	LUMIX_DELETE(m_allocator, m_root);
	ASSERT(isEmpty());
}

void Controller::unload() {
	for (const AnimationEntry& entry : m_animation_entries) {
		if (entry.animation) entry.animation->decRefCount();
	}
	m_animation_entries.clear();
	m_bone_masks.clear();
	m_inputs.clear();
	LUMIX_DELETE(m_allocator, m_root);
	m_root = nullptr;
}

bool Controller::load(Span<const u8> mem) {
	InputMemoryStream str(mem);
	return deserialize(str);
}

void Controller::destroyRuntime(RuntimeContext& ctx) {
	LUMIX_DELETE(m_allocator, &ctx);
}

RuntimeContext* Controller::createRuntime(u32 anim_set) {
	RuntimeContext* ctx = LUMIX_NEW(m_allocator, RuntimeContext)(*this, m_allocator);
	ctx->inputs.resize(m_inputs.size());
	memset(ctx->inputs.begin(), 0, ctx->inputs.byte_size());
	for (u32 i = 0; i < (u32)m_inputs.size(); ++i) {
		ctx->inputs[i].type = m_inputs[i].type;
	}
	ctx->animations.resize(m_animation_slots_count);
	memset(ctx->animations.begin(), 0, ctx->animations.byte_size());
	for (AnimationEntry& anim : m_animation_entries) {
		if (anim.set == anim_set) {
			ctx->animations[anim.slot] = anim.animation;
		}
	}
	if (m_root) m_root->enter(*ctx);
	return ctx;
}

void Controller::update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const {
	ASSERT(&ctx.controller == this);
	// TODO better allocation strategy
	const Span<u8> mem = ctx.data.releaseOwnership();
	ctx.data.reserve(mem.length());
	ctx.blendstack.clear();
	ctx.input_runtime.set(mem.begin(), mem.length());
	if (m_root) m_root->update(ctx, root_motion);

	m_allocator.deallocate(mem.begin());
}

struct Header {
	u32 magic = MAGIC;
	ControllerVersion version = ControllerVersion::LATEST;

	static constexpr u32 MAGIC = '_LAC';
};

void Controller::serialize(OutputMemoryStream& stream) {
	Header header;
	stream.write(header);
	stream.write(m_root_motion_bone);
	stream.writeArray(m_inputs);
	stream.write(m_animation_slots_count);
	stream.write((u32)m_animation_entries.size());
	for (const AnimationEntry& entry : m_animation_entries) {
		stream.write(entry.slot);
		stream.write(entry.set);
		stream.writeString(entry.animation ? entry.animation->getPath() : Path());
	}
	stream.write(m_ik.size());
	for (const IK& ik : m_ik) {
		stream.write(ik.max_iterations);
		stream.writeArray(ik.bones);
	}

	stream.write(m_root->type());
	m_root->serialize(stream);
}

bool Controller::deserialize(InputMemoryStream& stream) {
	Header header;
	stream.read(header);

	if (header.magic != Header::MAGIC) {
		logError("Invalid animation controller file ", getPath());
		return false;
	}
	if (header.version > ControllerVersion::LATEST)  {
		logError("Version of animation controller ", getPath(), " is not supported");
		return false;
	}

	stream.read(m_root_motion_bone);
	stream.readArray(&m_inputs);
	stream.read(m_animation_slots_count);

	const u32 entries_count = stream.read<u32>();
	m_animation_entries.reserve(entries_count);
	for (u32 i = 0; i < entries_count; ++i) {
		AnimationEntry& entry = m_animation_entries.emplace();
		stream.read(entry.slot);
		stream.read(entry.set);
		const char* path = stream.readString();
		entry.animation = path[0] ? m_resource_manager.getOwner().load<Animation>(Path(path)) : nullptr;
	}

	u32 ik_count = stream.read<u32>();
	m_ik.reserve(ik_count);
	for (u32 i = 0; i < ik_count; ++i) {
		IK& ik = m_ik.emplace(m_allocator);
		stream.read(ik.max_iterations);
		stream.readArray(&ik.bones);
	}

	NodeType type;
	stream.read(type);
	m_root = (PoseNode*)Node::create(type, *this);
	m_root->deserialize(stream, *this, (u32)header.version);
	return true;
}

static void getPose(const anim::RuntimeContext& ctx, Time time, float weight, u32 slot, Pose& pose, u32 mask_idx, bool looped) {
	Animation* anim = ctx.animations[slot];
	ASSERT(anim);
	ASSERT(ctx.model->isReady());
	ASSERT(anim->isReady());

	const Time anim_time = looped ? time % anim->getLength() : minimum(time, anim->getLength());

	Animation::SampleContext sample_ctx;
	sample_ctx.pose = &pose;
	sample_ctx.time = anim_time;
	sample_ctx.model = ctx.model;
	sample_ctx.weight = weight;
	sample_ctx.mask = mask_idx < (u32)ctx.controller.m_bone_masks.size() ? &ctx.controller.m_bone_masks[mask_idx] : nullptr;
	anim->setRootMotionBone(ctx.root_bone_hash);
	anim->getRelativePose(sample_ctx);
}

void evalBlendStack(const anim::RuntimeContext& ctx, Pose& pose) {
	InputMemoryStream bs(ctx.blendstack);

	for (;;) {
		anim::BlendStackInstructions instr;
		bs.read(instr);
		switch (instr) {
			case anim::BlendStackInstructions::END: return;
			case anim::BlendStackInstructions::SAMPLE: {
				u32 slot = bs.read<u32>();
				float weight = bs.read<float>();
				Time time = bs.read<Time>();
				bool looped = bs.read<bool>();
				getPose(ctx, time, weight, slot, pose, 0, looped);
				break;
			}
		}
	}
}

} // ns Lumix::Anim