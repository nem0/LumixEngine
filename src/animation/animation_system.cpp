#include "animation_scene.h"

#include "animation/animation.h"
#include "animation/property_animation.h"
#include "animation/controller.h"
#include "engine/base_proxy_allocator.h"
#include "engine/blob.h"
#include "engine/engine.h"
#include "engine/reflection.h"
#include "engine/universe/universe.h"
#include "renderer/model.h"
#include <cfloat>
#include <cmath>


namespace Lumix
{


enum class AnimationSceneVersion
{
	SHARED_CONTROLLER,

	LATEST
};


struct AnimSetProperty : public Reflection::IEnumProperty
{
	AnimSetProperty() { name = "Default set"; }


	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		AnimationScene* scene = static_cast<AnimationScene*>(cmp.scene);
		int value = scene->getControllerDefaultSet(cmp.entity);
		stream.write(value);
	}


	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		AnimationScene* scene = static_cast<AnimationScene*>(cmp.scene);
		int value = stream.read<int>();
		scene->setControllerDefaultSet(cmp.entity, value);
	}


	int getEnumValueIndex(ComponentUID cmp, int value) const override { return value; }
	int getEnumValue(ComponentUID cmp, int index) const override { return index; }


	int getEnumCount(ComponentUID cmp) const override
	{
		Anim::ControllerResource* res = static_cast<AnimationScene*>(cmp.scene)->getControllerResource(cmp.entity);
		return res ? res->m_sets_names.size() : 0;
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		Anim::ControllerResource* res = static_cast<AnimationScene*>(cmp.scene)->getControllerResource(cmp.entity);
		return res->m_sets_names[index];
	}
};


namespace FS
{
class FileSystem;
}


class Animation;
class Engine;
class JsonSerializer;
class Universe;


struct AnimationSystemImpl LUMIX_FINAL : public IPlugin
{
	void operator=(const AnimationSystemImpl&) = delete;
	AnimationSystemImpl(const AnimationSystemImpl&) = delete;

	explicit AnimationSystemImpl(Engine& engine);
	~AnimationSystemImpl();

	void registerLuaAPI() const;
	void createScenes(Universe& ctx) override;
	void destroyScene(IScene* scene) override;
	const char* getName() const override { return "animation"; }

	IAllocator& m_allocator;
	Engine& m_engine;
	AnimationManager m_animation_manager;
	PropertyAnimationManager m_property_animation_manager;
	Anim::ControllerManager m_controller_manager;

};


AnimationSystemImpl::AnimationSystemImpl(Engine& engine)
	: m_allocator(engine.getAllocator())
	, m_engine(engine)
	, m_animation_manager(m_allocator)
	, m_property_animation_manager(m_allocator)
	, m_controller_manager(m_allocator)
{
	m_animation_manager.create(Animation::TYPE, m_engine.getResourceManager());
	m_property_animation_manager.create(PropertyAnimation::TYPE, m_engine.getResourceManager());
	m_controller_manager.create(Anim::ControllerResource::TYPE, m_engine.getResourceManager());

	using namespace Reflection;
	static auto anim_scene = scene("animation",
		component("property_animator", 
			property("Animation", LUMIX_PROP(AnimationScene, PropertyAnimation),
				ResourceAttribute("Property animation (*.anp)", PropertyAnimation::TYPE)),
			property("Enabled", LUMIX_PROP_FULL(AnimationScene, isPropertyAnimatorEnabled, enablePropertyAnimator))
		),
		component("anim_controller",
			property("Source", LUMIX_PROP(AnimationScene, ControllerSource),
				ResourceAttribute("Animation controller (*.act)", Anim::ControllerResource::TYPE)),
			AnimSetProperty()
		),
		component("animable",
			property("Animation", LUMIX_PROP(AnimationScene, Animation),
				ResourceAttribute("Animation (*.ani)", Animation::TYPE)),
			property("Start time", LUMIX_PROP(AnimationScene, AnimableStartTime),
				MinAttribute(0)),
			property("Time scale", LUMIX_PROP(AnimationScene, AnimableTimeScale),
				MinAttribute(0))
		),
		component("shared_anim_controller",
			property("Parent", LUMIX_PROP(AnimationScene, SharedControllerParent))
		)
	);
	registerScene(anim_scene);

	registerLuaAPI();
}


AnimationSystemImpl::~AnimationSystemImpl()
{
	m_animation_manager.destroy();
	m_property_animation_manager.destroy();
	m_controller_manager.destroy();
}


void AnimationSystemImpl::registerLuaAPI() const
{
	AnimationScene::registerLuaAPI(m_engine.getState());
}


void AnimationSystemImpl::createScenes(Universe& ctx)
{
	AnimationScene* scene = AnimationScene::create(m_engine, *this, ctx, m_allocator);
	ctx.addScene(scene);
}


void AnimationSystemImpl::destroyScene(IScene* scene) { LUMIX_DELETE(m_allocator, scene); }


LUMIX_PLUGIN_ENTRY(animation)
{
	return LUMIX_NEW(engine.getAllocator(), AnimationSystemImpl)(engine);
}
}
