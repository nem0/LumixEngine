#include "animation_scene.h"

#include "animation/animation.h"
#include "animation/controller.h"
#include "animation/events.h"
#include "engine/base_proxy_allocator.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/json_serializer.h"
#include "engine/lua_wrapper.h"
#include "engine/job_system.h"
#include "engine/profiler.h"
#include "engine/properties.h"
#include "engine/resource_manager.h"
#include "engine/serializer.h"
#include "engine/universe/universe.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_scene.h"
#include <cfloat>
#include <cmath>


namespace Lumix
{


enum class AnimationSceneVersion
{
	SHARED_CONTROLLER,

	LATEST
};


static const ComponentType ANIMABLE_TYPE = Properties::getComponentType("animable");
static const ComponentType CONTROLLER_TYPE = Properties::getComponentType("anim_controller");
static const ComponentType SHARED_CONTROLLER_TYPE = Properties::getComponentType("shared_anim_controller");
static const ResourceType ANIMATION_TYPE("animation");
static const ResourceType CONTROLLER_RESOURCE_TYPE("anim_controller");


struct AnimSetProperty : public Properties::IEnumProperty
{
	AnimSetProperty() { name = "Default set"; }


	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		AnimationScene* scene = static_cast<AnimationScene*>(cmp.scene);
		int value = scene->getControllerDefaultSet(cmp.handle);
		stream.write(value);
	}


	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		AnimationScene* scene = static_cast<AnimationScene*>(cmp.scene);
		int value = stream.read<int>();
		scene->setControllerDefaultSet(cmp.handle, value);
	}



	int getEnumCount(ComponentUID cmp) const override
	{
		Anim::ControllerResource* res = static_cast<AnimationScene*>(cmp.scene)->getControllerResource(cmp.handle);
		return res->m_sets_names.size();
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		Anim::ControllerResource* res = static_cast<AnimationScene*>(cmp.scene)->getControllerResource(cmp.handle);
		return res->m_sets_names[index];
	}
};


namespace FS
{
class FileSystem;
};


class Animation;
class Engine;
class JsonSerializer;
class Universe;


struct AnimationSystemImpl LUMIX_FINAL : public IPlugin
{
	explicit AnimationSystemImpl(Engine& engine);
	~AnimationSystemImpl();


	void registerLuaAPI();
	void createScenes(Universe& ctx) override;
	void destroyScene(IScene* scene) override;
	const char* getName() const override { return "animation"; }

	IAllocator& m_allocator;
	Engine& m_engine;
	AnimationManager m_animation_manager;
	Anim::ControllerManager m_controller_manager;

private:
	void operator=(const AnimationSystemImpl&);
	AnimationSystemImpl(const AnimationSystemImpl&);
};


AnimationSystemImpl::AnimationSystemImpl(Engine& engine)
	: m_allocator(engine.getAllocator())
	, m_engine(engine)
	, m_animation_manager(m_allocator)
	, m_controller_manager(m_allocator)
{
	m_animation_manager.create(ANIMATION_TYPE, m_engine.getResourceManager());
	m_controller_manager.create(CONTROLLER_RESOURCE_TYPE, m_engine.getResourceManager());

	using namespace Properties;
	static auto anim_controller = component("anim_controller",
		property("Source", &AnimationScene::getControllerSource, &AnimationScene::setControllerSource,
			ResourceAttribute("Animation controller (*.act)", CONTROLLER_RESOURCE_TYPE)),
		AnimSetProperty()
	);
	Properties::registerComponent(&anim_controller);

	static auto animable = component("animable",
		property("Animation", &AnimationScene::getAnimation, &AnimationScene::setAnimation,
			ResourceAttribute("Animation (*.ani)", ANIMATION_TYPE)),
		property("Start time", &AnimationScene::getAnimableStartTime, &AnimationScene::setAnimableStartTime,
			MinAttribute(0)),
		property("Time scale", &AnimationScene::getAnimableTimeScale, &AnimationScene::setAnimableTimeScale,
			MinAttribute(0))
	);
	Properties::registerComponent(&animable);

	static auto shared_anim_controller = component("shared_anim_controller",
		property("Parent", &AnimationScene::getSharedControllerParent, &AnimationScene::setSharedControllerParent)
	);
	Properties::registerComponent(&shared_anim_controller);

	registerLuaAPI();
}


AnimationSystemImpl::~AnimationSystemImpl()
{
	m_animation_manager.destroy();
	m_controller_manager.destroy();
}


void AnimationSystemImpl::registerLuaAPI()
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
