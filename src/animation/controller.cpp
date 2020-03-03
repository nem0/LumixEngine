#include "animation.h"
#include "controller.h"
#include "nodes.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "renderer/model.h"
#include "renderer/pose.h"


namespace Lumix::Anim {

const ResourceType Controller::TYPE = ResourceType("anim_controller");

Controller::Controller(const Path& path, ResourceManager& resource_manager, IAllocator& allocator) 
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_animation_slots(allocator)
	, m_animation_entries(allocator)
	, m_bone_masks(allocator)
{}

Controller::~Controller() {
	ASSERT(isEmpty());
}

void Controller::destroy() {
	unload();
}

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
	ctx->animations.resize(m_animation_slots.size());
	memset(ctx->animations.begin(), 0, ctx->animations.byte_size());
	for (AnimationEntry& anim : m_animation_entries) {
		if (anim.set == anim_set) {
			ctx->animations[anim.slot] = anim.animation;
		}
	}
	m_root->enter(*ctx);
	return ctx;
}

void Controller::update(RuntimeContext& ctx, Ref<LocalRigidTransform> root_motion) {
	ASSERT(&ctx.controller == this);
	// TODO better allocation strategy
	const Span<u8> mem = ctx.data.releaseOwnership();
	ctx.data.reserve(mem.length());
	ctx.input_runtime.set(mem.begin(), mem.length());
	m_root->update(ctx, root_motion);
	m_allocator.deallocate(mem.begin());
	
	auto root_bone_iter = ctx.model->getBoneIndex(ctx.root_bone_hash);
	if (root_bone_iter.isValid()) {
		const int root_bone_idx = root_bone_iter.value();
		const Model::Bone& bone = ctx.model->getBone(root_bone_idx);
		root_motion->rot = bone.transform.rot * root_motion->rot * bone.transform.rot.conjugated();
		root_motion->pos = bone.transform.rot.rotate(root_motion->pos);
	}
}

void Controller::getPose(RuntimeContext& ctx, Ref<Pose> pose) {
	ASSERT(&ctx.controller == this);
	ctx.input_runtime.set(ctx.data.getData(), ctx.data.getPos());
	
	LocalRigidTransform root_bind_pose;
	auto root_bone_iter = ctx.model->getBoneIndex(ctx.root_bone_hash);
	if (root_bone_iter.isValid()) {
		const int root_bone_idx = root_bone_iter.value();
		root_bind_pose.pos = pose->positions[root_bone_idx];
		root_bind_pose.rot = pose->rotations[root_bone_idx];
	}
	
	m_root->getPose(ctx, 1.f, pose, 0xffFFffFF);
	
	// TODO this should be in AnimationNode
	if (root_bone_iter.isValid()) {
		const int root_bone_idx = root_bone_iter.value();
		pose->positions[root_bone_idx] = root_bind_pose.pos;
		pose->rotations[root_bone_idx] = root_bind_pose.rot;
	}
}

struct Header {
	enum class Version : u32 {
		LATEST
	};

	u32 magic = MAGIC;
	Version version = Version::LATEST;

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
		stream.write(entry.slot);
		stream.write(entry.set);
		stream.writeString(entry.animation ? entry.animation->getPath().c_str() : "");
	}
	stream.write(m_ik);
	stream.write(m_ik_count);
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
	if (header.version > Header::Version::LATEST) {
		logError("Animation") << "Version of animation controller " << getPath() << " is not supported";
		return false;
	}
	initEmpty();
	const u32 slots_count = stream.read<u32>();
	m_animation_slots.reserve(slots_count);
	for (u32 i = 0; i < slots_count; ++i) {
		String& slot = m_animation_slots.emplace(m_allocator);
		const char* tmp = stream.readString();
		slot = tmp;
	}

	const u32 entries_count = stream.read<u32>();
	m_animation_entries.reserve(entries_count);
	for (u32 i = 0; i < entries_count; ++i) {
		AnimationEntry& entry = m_animation_entries.emplace();
		stream.read(entry.slot);
		stream.read(entry.set);
		const char* path = stream.readString();
		entry.animation = path[0] ? m_resource_manager.getOwner().load<Animation>(Path(path)) : nullptr;
	}

	stream.read(m_ik);
	stream.read(m_ik_count);
	m_root->deserialize(stream, *this);
	return true;
}


} // ns Lumix::Anim