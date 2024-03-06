#pragma once

#include "core/hash.h"
#include "core/stream.h"
#include "animation.h"
#include "engine/resource.h"

namespace Lumix {

struct BoneMask;
struct LocalRigidTransform;
struct Pose;

namespace anim {

struct Value {
	enum Type : u32 {
		NUMBER,
		BOOL,
		VEC3
	};
	Type type;
	union {
		float f;
		bool b;
		Vec3 v3;
	};

	Value() : f(0), type(NUMBER) {}
	Value(float f) : f(f), type(NUMBER) {}
	Value(bool b) : b(b), type(BOOL) {}
	Value(Vec3 v3) : v3(v3), type(VEC3) {}
	float toFloat() const { ASSERT(type == NUMBER); return f; }
	i32 toI32() const { ASSERT(type == NUMBER); return i32(f + 0.5f); }
	bool toBool() const { ASSERT(type == BOOL); return b; }
	Vec3 toVec3() const { ASSERT(type == VEC3); return v3; }
};

struct RuntimeContext {
	RuntimeContext(struct Controller& controller, IAllocator& allocator);

	void setInput(u32 input_idx, float value);
	void setInput(u32 input_idx, bool value);

	Controller& controller;
	Array<Value> inputs;
	Array<Animation*> animations;
	OutputMemoryStream data;
	OutputMemoryStream blendstack;

	float weight = 1;
	Time time_delta;
	Model* model = nullptr;
	InputMemoryStream input_runtime;
	LocalRigidTransform root_motion;
};

enum BlendStackInstructions : u8 {
	END,
	SAMPLE,
	IK
};

enum class ControllerVersion : u32 {
	FIRST,

	LATEST
};

void evalBlendStack(const anim::RuntimeContext& ctx, Pose& pose);

struct Controller final : Resource {
	Controller(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	~Controller();

	void serialize(OutputMemoryStream& stream);
	bool deserialize(InputMemoryStream& stream);

	RuntimeContext* createRuntime(u32 anim_set);
	void destroyRuntime(RuntimeContext& ctx);
	void update(RuntimeContext& ctx) const;

	ResourceType getType() const override { return TYPE; }
	static const ResourceType TYPE;

	struct AnimationEntry {
		u32 set;
		u32 slot;
		Animation* animation = nullptr;
	};

	struct Input {
		Value::Type type;
		StaticString<32> name;
	};

	IAllocator& m_allocator;
	struct PoseNode* m_root = nullptr;
	Array<AnimationEntry> m_animation_entries;
	Array<BoneMask> m_bone_masks;
	Array<Input> m_inputs;
	u32 m_animation_slots_count = 0;

private:
	void unload() override;
	bool load(Span<const u8> mem) override;
};

} // namespace anim
} // ns Lumix