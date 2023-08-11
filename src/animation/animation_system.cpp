#include "animation_module.h"

#include "animation/animation.h"
#include "animation/property_animation.h"
#include "animation/controller.h"
#include "engine/engine.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/world.h"


namespace Lumix
{

struct Animation;
struct Engine;
struct World;

template <typename T>
struct AnimResourceManager final : ResourceManager
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


struct AnimationSystemImpl final : ISystem
{
	void operator=(const AnimationSystemImpl&) = delete;
	AnimationSystemImpl(const AnimationSystemImpl&) = delete;

	explicit AnimationSystemImpl(Engine& engine);
	~AnimationSystemImpl();

	void createModules(World& world) override;
	const char* getName() const override { return "animation"; }
	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }

	TagAllocator m_allocator;
	Engine& m_engine;
	AnimResourceManager<Animation> m_animation_manager;
	AnimResourceManager<PropertyAnimation> m_property_animation_manager;
	AnimResourceManager<anim::Controller> m_controller_manager;
};


AnimationSystemImpl::AnimationSystemImpl(Engine& engine)
	: m_allocator(engine.getAllocator(), "animation")
	, m_engine(engine)
	, m_animation_manager(m_allocator)
	, m_property_animation_manager(m_allocator)
	, m_controller_manager(m_allocator)
{
	AnimationModule::reflect(engine);
	m_animation_manager.create(Animation::TYPE, m_engine.getResourceManager());
	m_property_animation_manager.create(PropertyAnimation::TYPE, m_engine.getResourceManager());
	m_controller_manager.create(anim::Controller::TYPE, m_engine.getResourceManager());
}


AnimationSystemImpl::~AnimationSystemImpl()
{
	m_animation_manager.destroy();
	m_property_animation_manager.destroy();
	m_controller_manager.destroy();
}


void AnimationSystemImpl::createModules(World& world)
{
	UniquePtr<AnimationModule> module = AnimationModule::create(m_engine, *this, world, m_allocator);
	world.addModule(module.move());
}


LUMIX_PLUGIN_ENTRY(animation) {
	PROFILE_FUNCTION();
	return LUMIX_NEW(engine.getAllocator(), AnimationSystemImpl)(engine);
}
}
