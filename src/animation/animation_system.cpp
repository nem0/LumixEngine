#include "animation_system.h"
#include "animation/animation.h"
#include "engine/base_proxy_allocator.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/json_serializer.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/engine.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_scene.h"
#include "engine/universe/universe.h"
#include <cfloat>


namespace Lumix
{

static const ComponentType ANIMABLE_TYPE = PropertyRegister::getComponentType("animable");
static const uint32 ANIMATION_HASH = crc32("ANIMATION");

namespace FS
{
class FileSystem;
};

class Animation;
class Engine;
class JsonSerializer;
class Universe;


enum class AnimationSceneVersion : int
{
	FIRST,
	REFACTOR,

	LATEST
};


struct AnimationSceneImpl : public AnimationScene
{
	friend struct AnimationSystemImpl;

	struct Animable
	{
		float time;
		float time_scale;
		float start_time;
		class Animation* animation;
		Entity entity;
	};


	AnimationSceneImpl(IPlugin& anim_system, Engine& engine, Universe& universe, IAllocator& allocator)
		: m_universe(universe)
		, m_engine(engine)
		, m_anim_system(anim_system)
		, m_animables(allocator)
	{
		m_is_game_running = false;
		m_render_scene = nullptr;
		uint32 hash = crc32("renderer");
		for (auto* scene : universe.getScenes())
		{
			if (crc32(scene->getPlugin().getName()) == hash)
			{
				m_render_scene = static_cast<RenderScene*>(scene);
				break;
			}
		}
		ASSERT(m_render_scene);
	}


	~AnimationSceneImpl()
	{
		for (Animable& animable : m_animables)
		{
			unloadAnimation(animable.animation);
		}
	}


	float getAnimableTime(ComponentHandle cmp) override
	{
		return m_animables[{cmp.index}].time;
	}


	void setAnimableTime(ComponentHandle cmp, float time) override
	{
		m_animables[{cmp.index}].time = time;
	}


	Animation* getAnimableAnimation(ComponentHandle cmp) override
	{
		return m_animables[{cmp.index}].animation;
	}

	
	void startGame() override { m_is_game_running = true; }
	void stopGame() override { m_is_game_running = false; }
	Universe& getUniverse() override { return m_universe; }


	ComponentHandle getComponent(Entity entity, ComponentType type) override
	{
		if (type == ANIMABLE_TYPE)
		{
			if (m_animables.find(entity) < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		ASSERT(false);
		return INVALID_COMPONENT;
	}


	bool ownComponentType(ComponentType type) const override { return type == ANIMABLE_TYPE; }


	ComponentHandle createComponent(ComponentType type, Entity entity) override
	{
		if (type == ANIMABLE_TYPE) return createAnimable(entity);
		return INVALID_COMPONENT;
	}


	void unloadAnimation(Animation* animation)
	{
		if (!animation) return;

		auto& rm = animation->getResourceManager();
		auto* animation_manager = rm.get(ANIMATION_HASH);
		animation_manager->unload(*animation);
	}


	void destroyComponent(ComponentHandle component, ComponentType type) override
	{
		if (type == ANIMABLE_TYPE)
		{
			Entity entity = {component.index};
			auto& animable = m_animables[entity];
			unloadAnimation(animable.animation);
			m_animables.erase(entity);
			m_universe.destroyComponent(entity, type, this, component);
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write((int32)m_animables.size());
		for (const Animable& animable : m_animables)
		{
			serializer.write(animable.entity);
			serializer.write(animable.time_scale);
			serializer.write(animable.start_time);
			serializer.writeString(animable.animation ? animable.animation->getPath().c_str() : "");
		}
	}


	int getVersion() const override { return (int)AnimationSceneVersion::LATEST; }


	void deserialize(InputBlob& serializer, int version) override
	{
		int32 count;
		serializer.read(count);
		for (Animable& animable : m_animables)
		{
			unloadAnimation(animable.animation);
		}
		
		m_animables.clear();
		for (int i = 0; i < count; ++i)
		{
			Animable animable;
			serializer.read(animable.entity);
			bool free = false;
			if (version <= (int)AnimationSceneVersion::FIRST)
			{
				serializer.read(animable.time);
				serializer.read(free);
				animable.time_scale = 1;
				animable.start_time = 0;
			}
			else
			{
				uint32 flags = 0;
				if(version <= (int)AnimationSceneVersion::REFACTOR) serializer.read(flags);
				free = flags != 0;
				serializer.read(animable.time_scale);
				serializer.read(animable.start_time);
				animable.time = animable.start_time;
			}

			char path[MAX_PATH_LENGTH];
			serializer.readString(path, sizeof(path));
			animable.animation = path[0] == '\0' ? nullptr : loadAnimation(Path(path));
			if (!free)
			{
				m_animables.insert(animable.entity, animable);
				ComponentHandle cmp = {animable.entity.index};
				m_universe.addComponent(animable.entity, ANIMABLE_TYPE, this, cmp);
			}
		}
	}


	float getTimeScale(ComponentHandle cmp) { return m_animables[{cmp.index}].time_scale; }
	void setTimeScale(ComponentHandle cmp, float time_scale) { m_animables[{cmp.index}].time_scale = time_scale; }
	float getStartTime(ComponentHandle cmp) { return m_animables[{cmp.index}].start_time; }
	void setStartTime(ComponentHandle cmp, float time) { m_animables[{cmp.index}].start_time = time; }


	Path getAnimation(ComponentHandle cmp)
	{
		const auto& animable = m_animables[{cmp.index}];
		return animable.animation ? animable.animation->getPath() : Path("");
	}


	void setAnimation(ComponentHandle cmp, const Path& path)
	{
		auto& animable = m_animables[{cmp.index}];
		unloadAnimation(animable.animation);
		animable.animation = loadAnimation(path);
		animable.time = 0;
	}


	void updateAnimable(Animable& animable, float time_delta)
	{
		if (!animable.animation || !animable.animation->isReady()) return;
		ComponentHandle renderable = m_render_scene->getRenderableComponent(animable.entity);
		if (renderable == INVALID_COMPONENT) return;

		auto* pose = m_render_scene->getPose(renderable);
		auto* model = m_render_scene->getRenderableModel(renderable);

		if (!pose) return;
		if (!model->isReady()) return;

		model->getPose(*pose);
		pose->computeRelative(*model);
		animable.animation->getPose(animable.time, *pose, *model);

		float t = animable.time + time_delta * animable.time_scale;
		float l = animable.animation->getLength();
		while (t > l)
		{
			t -= l;
		}
		animable.time = t;
	}


	void updateAnimable(ComponentHandle cmp, float time_delta) override
	{
		Animable& animable = m_animables[{cmp.index}];
		updateAnimable(animable, time_delta);
	}


	void update(float time_delta, bool paused) override
	{
		PROFILE_FUNCTION();
		if (!m_is_game_running) return;

		for (Animable& animable : m_animables)
		{
			AnimationSceneImpl::updateAnimable(animable, time_delta);
		}
	}


	Animation* loadAnimation(const Path& path)
	{
		ResourceManager& rm = m_engine.getResourceManager();
		return static_cast<Animation*>(rm.get(ANIMATION_HASH)->load(path));
	}
	

	ComponentHandle createAnimable(Entity entity)
	{
		Animable animable;
		animable.time = 0;
		animable.animation = nullptr;
		animable.entity = entity;
		animable.time_scale = 1;
		animable.start_time = 0;
		m_animables.insert(entity, animable);

		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, ANIMABLE_TYPE, this, cmp);
		return cmp;
	}


	IPlugin& getPlugin() const override { return m_anim_system; }


	Universe& m_universe;
	IPlugin& m_anim_system;
	Engine& m_engine;
	AssociativeArray<Entity, Animable> m_animables;
	RenderScene* m_render_scene;
	bool m_is_game_running;
};


struct AnimationSystemImpl : public IPlugin
{
	explicit AnimationSystemImpl(Engine& engine)
		: m_allocator(engine.getAllocator())
		, m_engine(engine)
		, animation_manager(m_allocator)
	{
		animation_manager.create(ANIMATION_HASH, m_engine.getResourceManager());

		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationSceneImpl>)("Animation",
				&AnimationSceneImpl::getAnimation,
				&AnimationSceneImpl::setAnimation,
				"Animation (*.ani)",
				ANIMATION_HASH));
		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationSceneImpl>)(
				"Start time", &AnimationSceneImpl::getStartTime, &AnimationSceneImpl::setStartTime, 0, FLT_MAX, 0.1f));
		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationSceneImpl>)(
				"Time scale", &AnimationSceneImpl::getTimeScale, &AnimationSceneImpl::setTimeScale, 0, FLT_MAX, 0.1f));
	}

	IScene* createScene(Universe& ctx) override
	{
		return LUMIX_NEW(m_allocator, AnimationSceneImpl)(*this, m_engine, ctx, m_allocator);
	}


	void destroyScene(IScene* scene) override { LUMIX_DELETE(m_allocator, scene); }


	const char* getName() const override { return "animation"; }


	Lumix::IAllocator& m_allocator;
	Engine& m_engine;
	AnimationManager animation_manager;

private:
	void operator=(const AnimationSystemImpl&);
	AnimationSystemImpl(const AnimationSystemImpl&);
};


LUMIX_PLUGIN_ENTRY(animation)
{
	return LUMIX_NEW(engine.getAllocator(), AnimationSystemImpl)(engine);
}
}
