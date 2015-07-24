#include "animation_system.h"
#include "animation/animation.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/render_scene.h"
#include "universe/universe.h"


namespace Lumix
{

static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t ANIMABLE_HASH = crc32("animable");

namespace FS
{
class FileSystem;
};

class Animation;
class Engine;
class JsonSerializer;
class Universe;


class AnimationSceneImpl : public AnimationScene
{
private:
	struct Animable
	{
		bool m_manual;
		bool m_is_free;
		ComponentOld m_renderable;
		float m_time;
		class Animation* m_animation;
		Entity m_entity;
	};

public:
	AnimationSceneImpl(IPlugin& anim_system,
					   Engine& engine,
					   Universe& universe,
					   IAllocator& allocator)
		: m_universe(universe)
		, m_engine(engine)
		, m_anim_system(anim_system)
		, m_animables(allocator)
	{
		m_render_scene =
			static_cast<RenderScene*>(engine.getScene(crc32("renderer")));
		m_universe.componentCreated()
			.bind<AnimationSceneImpl, &AnimationSceneImpl::onComponentCreated>(
				this);
	}


	~AnimationSceneImpl()
	{
		m_universe.componentCreated()
			.unbind<AnimationSceneImpl,
					&AnimationSceneImpl::onComponentCreated>(this);
	}


	virtual Universe& getUniverse() override { return m_universe; }


	virtual bool ownComponentType(uint32_t type) const override
	{
		return type == ANIMABLE_HASH;
	}


	virtual ComponentOld getAnimable(Entity entity) override
	{
		for (int i = 0; i < m_animables.size(); ++i)
		{
			if (m_animables[i].m_entity == entity)
			{
				return ComponentOld(entity, ANIMABLE_HASH, this, i);
			}
		}
		return ComponentOld::INVALID;
	}


	virtual ComponentIndex createComponent(uint32_t type,
										   Entity entity) override
	{
		if (type == ANIMABLE_HASH)
		{
			return createAnimable(entity).index;
		}
		return NEW_INVALID_COMPONENT;
	}


	virtual void destroyComponent(ComponentIndex component, uint32_t type)
	{
		if (type == ANIMABLE_HASH)
		{
			m_animables[component].m_is_free = true;
			m_universe.destroyComponent(ComponentOld(
				m_animables[component].m_entity, type, this, component));
		}
	}


	virtual void serialize(OutputBlob& serializer) override
	{
		serializer.write((int32_t)m_animables.size());
		for (int i = 0; i < m_animables.size(); ++i)
		{
			serializer.write(m_animables[i].m_manual);
			serializer.write(m_animables[i].m_renderable.entity);
			serializer.write(m_animables[i].m_time);
			serializer.write(m_animables[i].m_is_free);
			serializer.writeString(
				m_animables[i].m_animation
					? m_animables[i].m_animation->getPath().c_str()
					: "");
		}
	}


	virtual void deserialize(InputBlob& serializer) override
	{
		int32_t count;
		serializer.read(count);
		m_animables.resize(count);
		for (int i = 0; i < count; ++i)
		{
			serializer.read(m_animables[i].m_manual);
			serializer.read(m_animables[i].m_entity);
			ComponentOld renderable =
				m_render_scene->getRenderableComponent(m_animables[i].m_entity);
			if (renderable.isValid())
			{
				m_animables[i].m_renderable = renderable;
			}
			serializer.read(m_animables[i].m_time);
			serializer.read(m_animables[i].m_is_free);
			char path[LUMIX_MAX_PATH];
			serializer.readString(path, sizeof(path));
			m_animables[i].m_animation =
				path[0] == '\0' ? NULL : loadAnimation(path);
			m_universe.addComponent(
				m_animables[i].m_entity, ANIMABLE_HASH, this, i);
		}
	}


	void setFrame(ComponentOld cmp, int frame)
	{
		m_animables[cmp.index].m_time =
			m_animables[cmp.index].m_animation->getLength() * frame /
			m_animables[cmp.index].m_animation->getFPS();
	}


	bool isManual(ComponentOld cmp) { return m_animables[cmp.index].m_manual; }


	void setManual(ComponentOld cmp, bool is_manual)
	{
		m_animables[cmp.index].m_manual = is_manual;
	}


	void getPreview(ComponentIndex cmp, string& path)
	{
		path = m_animables[cmp].m_animation
				   ? m_animables[cmp].m_animation->getPath().c_str()
				   : "";
	}


	void setPreview(ComponentIndex cmp, const string& path)
	{
		playAnimation(cmp, path.c_str());
	}


	virtual void playAnimation(ComponentIndex cmp, const char* path) override
	{
		m_animables[cmp].m_animation = loadAnimation(path);
		m_animables[cmp].m_time = 0;
		m_animables[cmp].m_manual = false;
	}


	void setAnimationFrame(const ComponentOld& cmp, int frame)
	{
		if (m_animables[cmp.index].m_animation)
		{
			m_animables[cmp.index].m_time =
				m_animables[cmp.index].m_animation->getLength() * frame /
				m_animables[cmp.index].m_animation->getFrameCount();
		}
	}


	int getFrameCount(const ComponentOld& cmp) const
	{
		if (m_animables[cmp.index].m_animation)
		{
			return m_animables[cmp.index].m_animation->getFrameCount();
		}
		return -1;
	}


	virtual void update(float time_delta) override
	{
		PROFILE_FUNCTION();
		if (m_animables.empty())
			return;
		for (int i = 0, c = m_animables.size(); i < c; ++i)
		{
			Animable& animable = m_animables[i];
			if (!animable.m_is_free && animable.m_animation &&
				animable.m_animation->isReady())
			{
				RenderScene* scene =
					static_cast<RenderScene*>(animable.m_renderable.scene);
				animable.m_animation->getPose(
					animable.m_time,
					scene->getPose(animable.m_renderable.index),
					*scene->getRenderableModel(animable.m_renderable.index));
				if (!animable.m_manual)
				{
					float t = animable.m_time + time_delta;
					float l = animable.m_animation->getLength();
					while (t > l)
					{
						t -= l;
					}
					animable.m_time = t;
				}
			}
		}
	}


private:
	Animation* loadAnimation(const char* path)
	{
		ResourceManager& rm = m_engine.getResourceManager();
		return static_cast<Animation*>(
			rm.get(ResourceManager::ANIMATION)->load(Path(path)));
	}


	void onComponentCreated(const ComponentOld& cmp)
	{
		if (cmp.type == RENDERABLE_HASH)
		{
			for (int i = 0; i < m_animables.size(); ++i)
			{
				if (m_animables[i].m_entity == cmp.entity)
				{
					m_animables[i].m_renderable = cmp;
					break;
				}
			}
		}
	}


	ComponentOld createAnimable(Entity entity)
	{
		Animable* src = NULL;
		for (int i = 0, c = m_animables.size(); i < c; ++i)
		{
			if (m_animables[i].m_is_free)
			{
				src = &m_animables[i];
				break;
			}
		}
		Animable& animable = src ? *src : m_animables.pushEmpty();
		animable.m_manual = true;
		animable.m_time = 0;
		animable.m_is_free = false;
		animable.m_renderable = ComponentOld::INVALID;
		animable.m_animation = NULL;
		animable.m_entity = entity;

		ComponentOld renderable = m_render_scene->getRenderableComponent(entity);
		if (renderable.isValid())
		{
			animable.m_renderable = renderable;
		}

		ComponentOld cmp = m_universe.addComponent(
			entity, ANIMABLE_HASH, this, m_animables.size() - 1);
		m_universe.componentCreated().invoke(cmp);
		return cmp;
	}


	virtual IPlugin& getPlugin() const override { return m_anim_system; }


private:
	Universe& m_universe;
	IPlugin& m_anim_system;
	Engine& m_engine;
	Array<Animable> m_animables;
	RenderScene* m_render_scene;
};


struct AnimationSystemImpl : public IPlugin
{
public:
	AnimationSystemImpl(Engine& engine)
		: m_allocator(engine.getAllocator())
		, m_engine(engine)
		, m_animation_manager(m_allocator)
	{
	}

	virtual IScene* createScene(Universe& universe) override
	{
		return m_allocator.newObject<AnimationSceneImpl>(
			*this, m_engine, universe, m_allocator);
	}


	virtual void destroyScene(IScene* scene) override
	{
		m_allocator.deleteObject(scene);
	}


	virtual const char* getName() const override { return "animation"; }


	virtual void setWorldEditor(WorldEditor& editor) override
	{
		IAllocator& allocator = editor.getAllocator();
		editor.registerComponentType("animable", "Animable");
		editor.registerProperty(
			"animable",
			allocator.newObject<FilePropertyDescriptor<AnimationSceneImpl>>(
				"preview",
				&AnimationSceneImpl::getPreview,
				&AnimationSceneImpl::setPreview,
				"Animation (*.ani)",
				allocator));
	}


	virtual bool create() override
	{
		m_animation_manager.create(ResourceManager::ANIMATION,
								   m_engine.getResourceManager());
		return true;
	}


	virtual void destroy() override {}

	BaseProxyAllocator m_allocator;
	Engine& m_engine;
	AnimationManager m_animation_manager;

private:
	void operator=(const AnimationSystemImpl&);
	AnimationSystemImpl(const AnimationSystemImpl&);
};


extern "C" IPlugin* createPlugin(Engine& engine)
{
	return engine.getAllocator().newObject<AnimationSystemImpl>(engine);
}


} // ~namespace Lumix
