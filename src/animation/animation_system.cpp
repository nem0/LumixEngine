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

	LATEST
};


struct AnimationSceneImpl : public AnimationScene
{
	friend struct AnimationSystemImpl;

	struct Animable
	{
		enum Flags : int
		{
			FREE = 1
		};

		uint32 flags;
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
		for (auto& animable : m_animables)
		{
			if (animable.flags & Animable::FREE) continue;
			unloadAnimation(animable.animation);
		}
	}


	float getAnimableTime(ComponentIndex cmp) override
	{
		return m_animables[cmp].time;
	}


	void setAnimableTime(ComponentIndex cmp, float time) override
	{
		m_animables[cmp].time = time;
	}


	Animation* getAnimableAnimation(ComponentIndex cmp) override
	{
		return m_animables[cmp].animation;
	}

	
	void startGame() override { m_is_game_running = true; }
	void stopGame() override { m_is_game_running = false; }
	Universe& getUniverse() override { return m_universe; }


	ComponentIndex getComponent(Entity entity, ComponentType type) override
	{
		ASSERT(ownComponentType(type));
		for (int i = 0; i < m_animables.size(); ++i)
		{
			if ((m_animables[i].flags & Animable::FREE) == 0 && m_animables[i].entity == entity) return i;
		}
		return INVALID_COMPONENT;
	}


	bool ownComponentType(ComponentType type) const override { return type == ANIMABLE_TYPE; }


	ComponentIndex createComponent(ComponentType type, Entity entity) override
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


	void destroyComponent(ComponentIndex component, ComponentType type) override
	{
		if (type == ANIMABLE_TYPE)
		{
			unloadAnimation(m_animables[component].animation);
			m_animables[component].flags |= Animable::FREE;
			m_universe.destroyComponent(m_animables[component].entity, type, this, component);
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write((int32)m_animables.size());
		for (const auto& animable : m_animables)
		{
			serializer.write(animable.entity);
			serializer.write(animable.flags);
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
		for (auto& animable : m_animables)
		{
			if ((animable.flags & Animable::FREE) == 0)
			{
				unloadAnimation(animable.animation);
				animable.animation = nullptr;
			}
		}
		
		m_animables.resize(count);
		for (int i = 0; i < count; ++i)
		{
			serializer.read(m_animables[i].entity);
			if (version <= (int)AnimationSceneVersion::FIRST)
			{
				serializer.read(m_animables[i].time);
				bool free;
				serializer.read(free);
				if (free)
					m_animables[i].flags |= Animable::FREE;
				else
					m_animables[i].flags &= ~Animable::FREE;
				m_animables[i].time_scale = 1;
				m_animables[i].start_time = 0;
			}
			else
			{
				serializer.read(m_animables[i].flags);
				serializer.read(m_animables[i].time_scale);
				serializer.read(m_animables[i].start_time);
				m_animables[i].time = m_animables[i].start_time;
			}

			char path[MAX_PATH_LENGTH];
			serializer.readString(path, sizeof(path));
			m_animables[i].animation = path[0] == '\0' ? nullptr : loadAnimation(Path(path));
			if ((m_animables[i].flags & Animable::FREE) == 0)
			{
				m_universe.addComponent(m_animables[i].entity, ANIMABLE_TYPE, this, i);
			}
		}
	}


	float getTimeScale(ComponentIndex cmp) { return m_animables[cmp].time_scale; }
	void setTimeScale(ComponentIndex cmp, float time_scale) { m_animables[cmp].time_scale = time_scale; }
	float getStartTime(ComponentIndex cmp) { return m_animables[cmp].start_time; }
	void setStartTime(ComponentIndex cmp, float time) { m_animables[cmp].start_time = time; }


	Path getAnimation(ComponentIndex cmp)
	{
		return m_animables[cmp].animation ? m_animables[cmp].animation->getPath() : Path("");
	}


	void setAnimation(ComponentIndex cmp, const Path& path)
	{
		unloadAnimation(m_animables[cmp].animation);
		m_animables[cmp].animation = loadAnimation(path);
		m_animables[cmp].time = 0;
	}


	void updateAnimable(ComponentIndex cmp, float time_delta) override
	{
		Animable& animable = m_animables[cmp];
		if ((animable.flags & Animable::FREE) != 0) return;
		if (!animable.animation || !animable.animation->isReady()) return;
		ComponentIndex renderable = m_render_scene->getRenderableComponent(animable.entity);
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


	void update(float time_delta, bool paused) override
	{
		PROFILE_FUNCTION();
		if (!m_is_game_running) return;

		for (int i = 0, c = m_animables.size(); i < c; ++i)
		{
			AnimationSceneImpl::updateAnimable(i, time_delta);
		}
	}


	Animation* loadAnimation(const Path& path)
	{
		ResourceManager& rm = m_engine.getResourceManager();
		return static_cast<Animation*>(rm.get(ANIMATION_HASH)->load(path));
	}
	

	ComponentIndex createAnimable(Entity entity)
	{
		Animable* src = nullptr;
		int cmp = m_animables.size();
		for (int i = 0, c = m_animables.size(); i < c; ++i)
		{
			if (m_animables[i].flags & Animable::FREE)
			{
				cmp = i;
				src = &m_animables[i];
				break;
			}
		}
		Animable& animable = src ? *src : m_animables.emplace();
		animable.time = 0;
		animable.flags &= ~Animable::FREE;
		animable.animation = nullptr;
		animable.entity = entity;
		animable.time_scale = 1;
		animable.start_time = 0;

		m_universe.addComponent(entity, ANIMABLE_TYPE, this, cmp);
		return cmp;
	}


	IPlugin& getPlugin() const override { return m_anim_system; }


	Universe& m_universe;
	IPlugin& m_anim_system;
	Engine& m_engine;
	Array<Animable> m_animables;
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
		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationSceneImpl>)("Animation",
								  &AnimationSceneImpl::getAnimation,
								  &AnimationSceneImpl::setAnimation,
								  "Animation (*.ani)",
								  ANIMATION_HASH,
								  m_allocator));
		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationSceneImpl>)("Start time",
								  &AnimationSceneImpl::getStartTime,
								  &AnimationSceneImpl::setStartTime,
								  0,
								  FLT_MAX,
								  0.1f,
								  m_allocator));
		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationSceneImpl>)("Time scale",
								  &AnimationSceneImpl::getTimeScale,
								  &AnimationSceneImpl::setTimeScale,
								  0,
								  FLT_MAX,
								  0.1f,
								  m_allocator));
	}

	IScene* createScene(Universe& ctx) override
	{
		return LUMIX_NEW(m_allocator, AnimationSceneImpl)(*this, m_engine, ctx, m_allocator);
	}


	void destroyScene(IScene* scene) override { LUMIX_DELETE(m_allocator, scene); }


	const char* getName() const override { return "animation"; }


	bool create() override
	{
		animation_manager.create(ANIMATION_HASH, m_engine.getResourceManager());
		return true;
	}


	void destroy() override {}

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
