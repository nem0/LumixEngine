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
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
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


static const ComponentType ANIMABLE_TYPE = PropertyRegister::getComponentType("animable");
static const ComponentType CONTROLLER_TYPE = PropertyRegister::getComponentType("anim_controller");
static const ComponentType SHARED_CONTROLLER_TYPE = PropertyRegister::getComponentType("shared_anim_controller");
static const ResourceType ANIMATION_TYPE("animation");
static const ResourceType CONTROLLER_RESOURCE_TYPE("anim_controller");


struct AnimSetPropertyDescriptor : public IEnumPropertyDescriptor
{
	AnimSetPropertyDescriptor(const char* name)
	{
		setName(name);
		m_type = ENUM;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		int value;
		stream.read(&value, sizeof(value));
		(static_cast<AnimationScene*>(cmp.scene)->setControllerDefaultSet)(cmp.handle, value);
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		int value;
		ASSERT(index == -1);
		value = (static_cast<AnimationScene*>(cmp.scene)->getControllerDefaultSet)(cmp.handle);
		stream.write(&value, sizeof(value));
	}


	int getEnumCount(IScene* scene, ComponentHandle cmp) override
	{
		Anim::ControllerResource* res = static_cast<AnimationScene*>(scene)->getControllerResource(cmp);
		return res->m_sets_names.size();
	}


	const char* getEnumItemName(IScene* scene, ComponentHandle cmp, int index) override
	{
		Anim::ControllerResource* res = static_cast<AnimationScene*>(scene)->getControllerResource(cmp);
		return res->m_sets_names[index];
	}


	void getEnumItemName(IScene* scene, ComponentHandle cmp, int index, char* buf, int max_size) override
	{
		Anim::ControllerResource* res = static_cast<AnimationScene*>(scene)->getControllerResource(cmp);
		copyString(buf, max_size, res->m_sets_names[index]);
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

	PropertyRegister::add("anim_controller",
		LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationScene>)("Source",
			&AnimationScene::getControllerSource,
			&AnimationScene::setControllerSource,
			"Animation controller (*.act)",
			CONTROLLER_RESOURCE_TYPE));
	PropertyRegister::add("anim_controller", LUMIX_NEW(m_allocator, AnimSetPropertyDescriptor)("Default set"));

	PropertyRegister::add("animable",
		LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationScene>)("Animation",
			&AnimationScene::getAnimation,
			&AnimationScene::setAnimation,
			"Animation (*.ani)",
			ANIMATION_TYPE));
	PropertyRegister::add("animable",
		LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationScene>)(
			"Start time", &AnimationScene::getAnimableStartTime, &AnimationScene::setAnimableStartTime, 0, FLT_MAX, 0.1f));
	PropertyRegister::add("animable",
		LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationScene>)(
			"Time scale", &AnimationScene::getAnimableTimeScale, &AnimationScene::setAnimableTimeScale, 0, FLT_MAX, 0.1f));

	PropertyRegister::add("shared_anim_controller",
		LUMIX_NEW(m_allocator, EntityPropertyDescriptor<AnimationScene>)(
			"Parent", &AnimationScene::getSharedControllerParent, &AnimationScene::setSharedControllerParent));


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
