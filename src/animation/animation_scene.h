#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"


struct lua_State;


namespace Lumix
{

class Animation;
struct IAllocator;
class OutputMemoryStream;
class Path;

namespace Anim
{
class Controller;
}


struct Animable
{
	Time time;
	Animation* animation;
	EntityRef entity;
};


struct AnimationScene : public IScene
{
	static AnimationScene* create(Engine& engine, IPlugin& plugin, Universe& universe, IAllocator& allocator);
	static void destroy(AnimationScene& scene);
	static void registerLuaAPI(lua_State* L);

	virtual const OutputMemoryStream& getEventStream() const = 0;
	virtual Path getPropertyAnimation(EntityRef entity) = 0;
	virtual void setPropertyAnimation(EntityRef entity, const Path& path) = 0;
	virtual bool isPropertyAnimatorEnabled(EntityRef entity) = 0;
	virtual void enablePropertyAnimator(EntityRef entity, bool enabled) = 0;
	virtual class Animation* getAnimableAnimation(EntityRef entity) = 0;
	virtual Path getAnimation(EntityRef entity) = 0;
	virtual void setAnimation(EntityRef entity, const Path& path) = 0;
	virtual void updateAnimable(EntityRef entity, float time_delta) = 0;
	virtual void updateAnimator(EntityRef entity, float time_delta) = 0;
	virtual Animable& getAnimable(EntityRef entity) = 0;
	virtual void setAnimatorInput(EntityRef entity, u32 input_idx, u32 value) = 0;
	virtual void setAnimatorInput(EntityRef entity, u32 input_idx, float value) = 0;
	virtual void setAnimatorInput(EntityRef entity, u32 input_idx, bool value) = 0;
	virtual struct LocalRigidTransform getAnimatorRootMotion(EntityRef entity) = 0;
	virtual void setAnimatorSource(EntityRef entity, const Path& path) = 0;
	virtual class Path getAnimatorSource(EntityRef entity) = 0;
	virtual int getAnimatorInputIndex(EntityRef entity, const char* name) const = 0;
	virtual void applyAnimatorSet(EntityRef entity, u32 idx) = 0;
	virtual void setAnimatorDefaultSet(EntityRef entity, u32 idx) = 0;
	virtual u32 getAnimatorDefaultSet(EntityRef entity) = 0;
	virtual float getAnimationLength(int animation_idx) = 0;
};


} // ~ namespace Lumix
