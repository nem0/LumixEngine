#include "animation.h"
#include "controller.h"
#include "nodes.h"
#include "engine/hash.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "renderer/model.h"
#include "renderer/pose.h"


namespace Lumix::anim {

const ResourceType Controller::TYPE = ResourceType("anim_controller");

Controller::Controller(const Path& path, ResourceManager& resource_manager, IAllocator& allocator) 
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_animation_slots(allocator)
	, m_animation_entries(allocator)
	, m_inputs(allocator)
	, m_bone_masks(allocator)
{
	m_root = LUMIX_NEW(m_allocator, TreeNode)(nullptr, *this, m_allocator);
	m_root->m_name = "Root";
}

Controller::~Controller() {
	LUMIX_DELETE(m_allocator, m_root);
	ASSERT(isEmpty());
}

void Controller::clear() {
	unload();
}

void Controller::unload() {
	for (const AnimationEntry& entry : m_animation_entries) {
		if (entry.animation) entry.animation->decRefCount();
	}
	m_animation_entries.clear();
	m_animation_slots.clear();
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
	if (!m_compiled) return nullptr;
	RuntimeContext* ctx = LUMIX_NEW(m_allocator, RuntimeContext)(*this, m_allocator);
	ctx->inputs.resize(m_inputs.size());
	memset(ctx->inputs.begin(), 0, ctx->inputs.byte_size());
	ctx->animations.resize(m_animation_slots.size());
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
	if (!m_compiled) return;
	ASSERT(&ctx.controller == this);
	// TODO better allocation strategy
	const Span<u8> mem = ctx.data.releaseOwnership();
	ctx.data.reserve(mem.length());
	ctx.events.clear();
	ctx.input_runtime.set(mem.begin(), mem.length());
	if (m_root) m_root->update(ctx, root_motion);

	m_allocator.deallocate(mem.begin());
}

void Controller::getPose(RuntimeContext& ctx, Pose& pose) {
	if (!m_compiled) return;
	ASSERT(&ctx.controller == this);
	ctx.input_runtime.set(ctx.data.data(), ctx.data.size());
	if (m_root) m_root->getPose(ctx, 1.f, pose, 0xffFFffFF);
}

struct Header {

	u32 magic = MAGIC;
	ControllerVersion version = ControllerVersion::LATEST;

	static constexpr u32 MAGIC = '_LAC';
};

void Controller::serialize(OutputMemoryStream& stream) {
	Header header;
	stream.write(header);
	stream.write(m_flags);
	stream.write(m_id_generator);
	stream.write(m_root_motion_bone);
	stream.writeArray(m_inputs);
	stream.write((u32)m_animation_slots.size());
	for (const String& slot : m_animation_slots) {
		stream.writeString(slot);
	}
	stream.write((u32)m_animation_entries.size());
	for (const AnimationEntry& entry : m_animation_entries) {
		stream.write(entry.slot);
		stream.write(entry.set);
		stream.writeString(entry.animation ? entry.animation->getPath() : Path());
	}
	stream.write(m_ik);
	stream.write(m_ik_count);
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

	if (header.version <= ControllerVersion::FIRST_SUPPORTED) {
		logError("Version of animation controller ", getPath(), " is too old and not supported");
		return false;
	}

	stream.read(m_flags);
	stream.read(m_id_generator);
	stream.read(m_root_motion_bone);
	stream.readArray(&m_inputs);
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
	m_root = LUMIX_NEW(m_allocator, TreeNode)(nullptr, *this, m_allocator);
	m_root->m_name = "Root";
	m_root->deserialize(stream, *this, (u32)header.version);
	if (m_root->compile()) {
		m_compiled = true;
	}
	else {
		logError("Failed to compile ", m_path);
		m_compiled = false;
	}
	return true;
}


} // ns Lumix::Anim