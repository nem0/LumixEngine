#include "core/hash.h"
#include "core/log.h"
#include "core/stack_array.h"
#include "animation.h"
#include "controller.h"
#include "nodes.h"
#include "engine/resource_manager.h"
#include "renderer/model.h"
#include "renderer/pose.h"


namespace black::anim {

const ResourceType Controller::TYPE = ResourceType("anim_controller");

Controller::Controller(const Path& path, ResourceManager& resource_manager, IAllocator& allocator) 
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_animation_entries(allocator)
	, m_inputs(allocator)
	, m_bone_masks(allocator)
{}

Controller::~Controller() {
	unload();
}

void Controller::unload() {
	for (const AnimationEntry& entry : m_animation_entries) {
		if (entry.animation) {
			removeDependency(*entry.animation);
			entry.animation->decRefCount();
		}
	}
	m_animation_entries.clear();
	m_bone_masks.clear();
	m_inputs.clear();
	BLACK_DELETE(m_allocator, m_root);
	m_root = nullptr;
}

bool Controller::load(Span<const u8> mem) {
	InputMemoryStream str(mem);
	return deserialize(str);
}

void Controller::destroyRuntime(RuntimeContext& ctx) {
	BLACK_DELETE(m_allocator, &ctx);
}

RuntimeContext* Controller::createRuntime(u32 anim_set) {
	RuntimeContext* ctx = BLACK_NEW(m_allocator, RuntimeContext)(*this, m_allocator);
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

void Controller::update(RuntimeContext& ctx) const {
	ASSERT(&ctx.controller == this);
	// TODO better allocation strategy
	const Span<u8> mem = ctx.data.releaseOwnership();
	ctx.data.reserve(mem.length());
	ctx.blendstack.clear();
	ctx.input_runtime.set(mem.begin(), mem.length());
	if (m_root) m_root->update(ctx);

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
	stream.writeArray(m_inputs);
	stream.write(m_animation_slots_count);
	stream.write((u32)m_animation_entries.size());
	for (const AnimationEntry& entry : m_animation_entries) {
		stream.write(entry.slot);
		stream.write(entry.set);
		stream.writeString(entry.animation ? entry.animation->getPath() : Path());
	}

	stream.write(m_root->type());
	m_root->serialize(stream);
}

bool Controller::deserialize(InputMemoryStream& stream) {
	unload();

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
		if(entry.animation) {
			addDependency(*entry.animation);
		}
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
	anim->getRelativePose(sample_ctx);
}

static LocalRigidTransform getAbsolutePosition(const Pose& pose, const Model& model, int bone_index) {
	i32 parent_idx = model.getBoneParent(bone_index);
	LocalRigidTransform bone_transform{pose.positions[bone_index], pose.rotations[bone_index]};
	if (parent_idx < 0) return bone_transform;
	return getAbsolutePosition(pose, model, parent_idx) * bone_transform;
}

void evalIK(float alpha, Vec3 target, BoneNameHash leaf_bone, u32 bones_count, Model& model, Pose& pose, const Path& controller_path) {
	if (alpha < 0.001f) return;
	
	// TODO user defined
	const u32 max_iterations = 5;
	enum { MAX_BONES_COUNT = 32 };
	ASSERT(bones_count <= MAX_BONES_COUNT);

	u32 indices[MAX_BONES_COUNT];
	LocalRigidTransform transforms[MAX_BONES_COUNT];
	Vec3 old_pos[MAX_BONES_COUNT];
	float len[MAX_BONES_COUNT - 1];
	float len_sum = 0;
	auto leaf_iter = model.getBoneIndex(leaf_bone);
	if (!leaf_iter.isValid()) {
		logError(controller_path, ": IK leaf bone not found in ", model.getPath());
		return;
	}
	indices[bones_count - 1] = leaf_iter.value();
	for (u32 i = 1; i < bones_count; ++i) {
		indices[bones_count - 1 - i] = model.getBoneParent(indices[bones_count - i]);
	}

	// convert from bone space to object space
	const i32 first_bone_parent = model.getBoneParent(indices[0]);
	LocalRigidTransform roots_parent;
	if (first_bone_parent >= 0) {
		roots_parent = getAbsolutePosition(pose, model, first_bone_parent);
	}
	else {
		roots_parent.pos = Vec3::ZERO;
		roots_parent.rot = Quat::IDENTITY;
	}

	LocalRigidTransform parent_tr = roots_parent;
	for (u32 i = 0; i < bones_count; ++i) {
		LocalRigidTransform tr{pose.positions[indices[i]], pose.rotations[indices[i]]};
		transforms[i] = parent_tr * tr;
		old_pos[i] = transforms[i].pos;
		if (i > 0) {
			len[i - 1] = length(transforms[i].pos - transforms[i - 1].pos);
			len_sum += len[i - 1];
		}
		parent_tr = transforms[i];
	}

	Vec3 to_target = target - transforms[0].pos;
	if (len_sum * len_sum < squaredLength(to_target)) {
		to_target = normalize(to_target);
		target = transforms[0].pos + to_target * len_sum;
	}

	for (u32 iteration = 0; iteration < max_iterations; ++iteration) {
		transforms[bones_count - 1].pos = target;
			
		for (i32 i = bones_count - 1; i > 1; --i) {
			Vec3 dir = normalize((transforms[i - 1].pos - transforms[i].pos));
			transforms[i - 1].pos = transforms[i].pos + dir * len[i - 1];
		}

		for (u32 i = 1; i < bones_count; ++i) {
			Vec3 dir = normalize((transforms[i].pos - transforms[i - 1].pos));
			transforms[i].pos = transforms[i - 1].pos + dir * len[i - 1];
		}
	}

	// compute rotations from new positions
	for (i32 i = bones_count - 2; i >= 0; --i) {
		Vec3 old_d = old_pos[i + 1] - old_pos[i];
		Vec3 new_d = transforms[i + 1].pos - transforms[i].pos;

		Quat rel_rot = Quat::vec3ToVec3(old_d, new_d);
		transforms[i].rot = rel_rot * transforms[i].rot;
	}

	// convert from object space to bone space
	LocalRigidTransform ik_out[MAX_BONES_COUNT];
	for (i32 i = bones_count - 1; i > 0; --i) {
		transforms[i] = transforms[i - 1].inverted() * transforms[i];
		ik_out[i].pos = transforms[i].pos;
	}
	for (i32 i = bones_count - 2; i > 0; --i) {
		ik_out[i].rot = transforms[i].rot;
	}
	ik_out[bones_count - 1].rot = pose.rotations[indices[bones_count - 1]];

	if (first_bone_parent >= 0) {
		ik_out[0].rot = roots_parent.rot.conjugated() * transforms[0].rot;
	}
	else {
		ik_out[0].rot = transforms[0].rot;
	}
	ik_out[0].pos = pose.positions[indices[0]];

	for (u32 i = 0; i < bones_count; ++i) {
		const u32 idx = indices[i];
		pose.positions[idx] = lerp(pose.positions[idx], ik_out[i].pos, alpha);
		pose.rotations[idx] = nlerp(pose.rotations[idx], ik_out[i].rot, alpha);
	}
}

void evalBlendStack(const anim::RuntimeContext& ctx, Pose& pose) {
	InputMemoryStream bs(ctx.blendstack);

	for (;;) {
		anim::BlendStackInstructions instr;
		bs.read(instr);
		switch (instr) {
			case anim::BlendStackInstructions::END: return;
			case anim::BlendStackInstructions::IK: {
				float alpha = bs.read<float>();
				Vec3 pos = bs.read<Vec3>();
				BoneNameHash leaf_bone = bs.read<BoneNameHash>();
				u32 bone_count = bs.read<u32>();
				evalIK(alpha * ctx.weight, pos, leaf_bone, bone_count, *ctx.model, pose, ctx.controller.getPath());
				break;
			}
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

} // ns black.h::Anim