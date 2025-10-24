#pragma once

#include "engine/lumix.h"

#include "core/allocator.h"

#include "animation/animation.h"
#include "engine/plugin.h"

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

//@ module AnimationModule animation "Animation"
struct AnimationModule : IModule {
	static UniquePtr<AnimationModule> create(Engine& engine, ISystem& system, World& world, struct IAllocator& allocator);
	static void reflect(Engine& engine);

	virtual void createAnimable(EntityRef entity) = 0;
	virtual void createAnimator(EntityRef entity) = 0;
	virtual void createPropertyAnimator(EntityRef entity) = 0;
	virtual void destroyAnimable(EntityRef entity) = 0;
	virtual void destroyAnimator(EntityRef entity) = 0;
	virtual void destroyPropertyAnimator(EntityRef entity) = 0;

	//@ component PropertyAnimator
	virtual bool isPropertyAnimatorEnabled(EntityRef entity) = 0;
	virtual void enablePropertyAnimator(EntityRef entity, bool enabled) = 0;
	virtual bool getPropertyAnimatorLooped(EntityRef entity) = 0;
	virtual void setPropertyAnimatorLooped(EntityRef entity, bool looped) = 0;
	virtual Path getPropertyAnimatorAnimation(EntityRef entity) = 0;					//@ resource_type PropertyAnimation::TYPE
	virtual void setPropertyAnimatorAnimation(EntityRef entity, const Path& path) = 0;
	//@ end
	
	//@ component Animator
	virtual void setAnimatorSource(EntityRef entity, const Path& path) = 0;				//@ resource_type anim::Controller::TYPE
	virtual Path getAnimatorSource(EntityRef entity) = 0;
	virtual void setAnimatorUseRootMotion(EntityRef entity, bool value) = 0;
	virtual bool getAnimatorUseRootMotion(EntityRef entity) = 0;
	virtual void setAnimatorDefaultSet(EntityRef entity, u32 idx) = 0;
	virtual u32 getAnimatorDefaultSet(EntityRef entity) = 0;
	virtual void applyAnimatorSet(EntityRef entity, u32 idx) = 0;						//@ function alias applySet
	
	virtual void setAnimatorInput(EntityRef entity, u32 input_idx, bool value) = 0;		//@ function alias setBoolInput
	virtual void setAnimatorInput(EntityRef entity, u32 input_idx, float value) = 0;	//@ function alias setFloatInput
	virtual void setAnimatorInput(EntityRef entity, u32 input_idx, Vec3 value) = 0;		//@ function alias setVec3Input
	virtual int getAnimatorInputIndex(EntityRef entity, const char* name) const = 0;	//@ function alias getInputIndex
	//@ end
	virtual void updateAnimator(EntityRef entity, float time_delta) = 0;
	virtual bool getAnimatorBoolInput(EntityRef entity, u32 input_idx) = 0;
	virtual float getAnimatorFloatInput(EntityRef entity, u32 input_idx) = 0;
	virtual Vec3 getAnimatorVec3Input(EntityRef entity, u32 input_idx) = 0;
	
	
	virtual struct LocalRigidTransform getAnimatorRootMotion(EntityRef entity) = 0;
	virtual anim::Controller* getAnimatorController(EntityRef entity) = 0;
	virtual anim::RuntimeContext* getAnimatorRuntimeContext(EntityRef entity) = 0;
	virtual OutputMemoryStream& beginBlendstackUpdate(EntityRef entity) = 0;
	virtual void endBlendstackUpdate(EntityRef entity) = 0;

	//@ component Animable
	virtual Path getAnimableAnimation(EntityRef entity) = 0;						//@ resource_type Animation::TYPE
	virtual void setAnimableAnimation(EntityRef entity, const Path& path) = 0;
	//@ end
	virtual void updateAnimable(EntityRef entity, float time_delta) = 0;

	virtual struct Animation* getAnimation(EntityRef entity) = 0;
	virtual Animable& getAnimable(EntityRef entity) = 0;
};


} // namespace Lumix
