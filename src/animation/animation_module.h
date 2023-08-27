#pragma once

#include "animation/animation.h"
#include "engine/allocator.h"
#include "engine/lumix.h"
#include "engine/plugin.h"

struct lua_State;

namespace Lumix {

namespace anim {
	struct Controller;
	struct RuntimeContext;
}

struct Animable {
	Time time;
	struct Animation* animation;
	EntityRef entity;
};

struct AnimationModule : IModule {
	static UniquePtr<AnimationModule> create(Engine& engine, ISystem& system, World& world, struct IAllocator& allocator);
	static void reflect(Engine& engine);

	virtual struct Path getPropertyAnimation(EntityRef entity) = 0;
	virtual void setPropertyAnimation(EntityRef entity, const Path& path) = 0;
	virtual bool isPropertyAnimatorEnabled(EntityRef entity) = 0;
	virtual void enablePropertyAnimator(EntityRef entity, bool enabled) = 0;
	virtual struct Animation* getAnimableAnimation(EntityRef entity) = 0;
	virtual Path getAnimation(EntityRef entity) = 0;
	virtual void setAnimation(EntityRef entity, const Path& path) = 0;
	virtual void updateAnimable(EntityRef entity, float time_delta) = 0;
	virtual void updateAnimator(EntityRef entity, float time_delta) = 0;
	virtual Animable& getAnimable(EntityRef entity) = 0;
	virtual void setAnimatorInput(EntityRef entity, u32 input_idx, i32 value) = 0;
	virtual void setAnimatorInput(EntityRef entity, u32 input_idx, float value) = 0;
	virtual void setAnimatorInput(EntityRef entity, u32 input_idx, bool value) = 0;
	virtual float getAnimatorFloatInput(EntityRef entity, u32 input_idx) = 0;
	virtual bool getAnimatorBoolInput(EntityRef entity, u32 input_idx) = 0;
	virtual i32 getAnimatorI32Input(EntityRef entity, u32 input_idx) = 0;
	virtual struct LocalRigidTransform getAnimatorRootMotion(EntityRef entity) = 0;
	virtual void setAnimatorUseRootMotion(EntityRef entity, bool value) = 0;
	virtual bool getAnimatorUseRootMotion(EntityRef entity) = 0;
	virtual void setAnimatorSource(EntityRef entity, const Path& path) = 0;
	virtual struct Path getAnimatorSource(EntityRef entity) = 0;
	virtual int getAnimatorInputIndex(EntityRef entity, const char* name) const = 0;
	virtual void applyAnimatorSet(EntityRef entity, u32 idx) = 0;
	virtual void setAnimatorDefaultSet(EntityRef entity, u32 idx) = 0;
	virtual u32 getAnimatorDefaultSet(EntityRef entity) = 0;
	virtual anim::Controller* getAnimatorController(EntityRef entity) = 0;
	virtual anim::RuntimeContext* getAnimatorRuntimeContext(EntityRef entity) = 0;
	virtual void setAnimatorIK(EntityRef entity, u32 index, float weight, const struct Vec3& target) = 0;
	virtual float getAnimationLength(int animation_idx) = 0;
	virtual OutputMemoryStream& beginBlendstackUpdate(EntityRef entity) = 0;
	virtual void endBlendstackUpdate(EntityRef entity) = 0;
};


} // namespace Lumix
