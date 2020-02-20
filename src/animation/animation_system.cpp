#include "animation_scene.h"

#include "animation/animation.h"
#include "animation/property_animation.h"
#include "animation/controller.h"
#include "engine/engine.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"


namespace Lumix
{

class Animation;
class Engine;
class Universe;

enum class AnimationSceneVersion
{
	SHARED_CONTROLLER,

	LATEST
};

/*
struct AnimSetProperty : public Reflection::IEnumProperty
{
	AnimSetProperty() 
	{ 
		name = "Default set"; 
	}


	void getValue(ComponentUID cmp, int index, IOutputStream& stream) const override
	{
		AnimationScene* scene = static_cast<AnimationScene*>(cmp.scene);
		int value = scene->getControllerDefaultSet((EntityRef)cmp.entity);
		stream.write(value);
	}


	void setValue(ComponentUID cmp, int index, InputMemoryStream& stream) const override
	{
		AnimationScene* scene = static_cast<AnimationScene*>(cmp.scene);
		int value = stream.read<int>();
		scene->setControllerDefaultSet((EntityRef)cmp.entity, value);
	}


	int getEnumValueIndex(ComponentUID cmp, int value) const override { return value; }
	int getEnumValue(ComponentUID cmp, int index) const override { return index; }


	int getEnumCount(ComponentUID cmp) const override
	{
		Anim::ControllerResource* res = static_cast<AnimationScene*>(cmp.scene)->getControllerResource((EntityRef)cmp.entity);
		return res ? res->m_sets_names.size() : 0;
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		Anim::ControllerResource* res = static_cast<AnimationScene*>(cmp.scene)->getControllerResource((EntityRef)cmp.entity);
		return res->m_sets_names[index];
	}
};
*/

template <typename T>
struct AnimResourceManager final : public ResourceManager
{
	explicit AnimResourceManager(IAllocator& allocator)
		: ResourceManager(allocator)
		, m_allocator(allocator)
	{}

	Resource* createResource(const Path& path) override {
		return LUMIX_NEW(m_allocator, T)(path, *this, m_allocator);
	}

	void destroyResource(Resource& resource) override {
		LUMIX_DELETE(m_allocator, static_cast<T*>(&resource));
	}

	IAllocator& m_allocator;
};


struct AnimationSystemImpl final : public IPlugin
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
	AnimResourceManager<Animation> m_animation_manager;
	AnimResourceManager<PropertyAnimation> m_property_animation_manager;
	AnimResourceManager<Anim::Controller> m_controller_manager;
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
	m_controller_manager.create(Anim::Controller::TYPE, m_engine.getResourceManager());

	using namespace Reflection;
	static auto anim_scene = scene("animation",
		component("property_animator", 
			property("Animation", LUMIX_PROP(AnimationScene, PropertyAnimation),
				ResourceAttribute("Property animation (*.anp)", PropertyAnimation::TYPE)),
			property("Enabled", &AnimationScene::isPropertyAnimatorEnabled, &AnimationScene::enablePropertyAnimator)
		),
		component("animator",
			property("Source", LUMIX_PROP(AnimationScene, AnimatorSource),
				ResourceAttribute("Animation controller (*.act)", Anim::Controller::TYPE)),
			property("Default set", LUMIX_PROP(AnimationScene, AnimatorDefaultSet))
		),
		component("animable",
			property("Animation", LUMIX_PROP(AnimationScene, Animation),
				ResourceAttribute("Animation (*.ani)", Animation::TYPE))
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
