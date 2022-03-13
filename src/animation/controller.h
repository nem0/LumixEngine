#pragma once

#include "condition.h"
#include "engine/flag_set.h"
#include "engine/hash.h"
#include "engine/resource.h"
#include "engine/stream.h"

namespace Lumix {

struct BoneMask;
struct LocalRigidTransform;
struct Pose;

namespace anim {

struct GroupNode;
struct RuntimeContext;

enum class ControllerVersion : u32 {
	EVENTS,
	TRANSITIONS,
	HASH64,

	LATEST
};

struct Controller final : Resource {
public:
	Controller(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	~Controller();

	void serialize(OutputMemoryStream& stream);
	bool deserialize(InputMemoryStream& stream);

	RuntimeContext* createRuntime(u32 anim_set);
	void destroyRuntime(RuntimeContext& ctx);
	void update(RuntimeContext& ctx, LocalRigidTransform& root_motion) const;
	void getPose(RuntimeContext& ctx, struct Pose& pose);
	void initEmpty();
	void destroy();

	ResourceType getType() const override { return TYPE; }
	static const ResourceType TYPE;

	struct AnimationEntry {
		u32 set;
		u32 slot;
		Animation* animation;
	};

	IAllocator& m_allocator;
	GroupNode* m_root = nullptr;
	Array<AnimationEntry> m_animation_entries;
	Array<String> m_animation_slots;
	Array<BoneMask> m_bone_masks;
	InputDecl m_inputs;
	enum class Flags : u32 {
		XZ_ROOT_MOTION = 1 << 0
	};
	FlagSet<Flags, u32> m_flags;
	struct IK {
		enum { MAX_BONES_COUNT = 8 };
		u16 max_iterations = 5;
		u16 bones_count = 4;
		BoneNameHash bones[MAX_BONES_COUNT];
	} m_ik[4];
	u32 m_ik_count = 0;
	StaticString<64> m_root_motion_bone;

private:
	void processEvents(RuntimeContext& ctx) const;
	void unload() override;
	bool load(u64 size, const u8* mem) override;
};

} // namespace anim
} // ns Lumix