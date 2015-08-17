#include "animation_system.h"
#include "animation/animation.h"
#include "core/base_proxy_allocator.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "engine/property_descriptor.h"
#include "renderer/render_scene.h"
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


class AnimationSceneImpl : public IScene
{
private:
	struct Animable
	{
		bool m_is_free;
		ComponentIndex m_renderable;
		float m_time;
		class Animation* m_animation;
		Entity m_entity;
	};

public:
	AnimationSceneImpl(IPlugin& anim_system,
					   Engine& engine,
					   UniverseContext& ctx,
					   IAllocator& allocator)
		: m_universe(*ctx.m_universe)
		, m_engine(engine)
		, m_anim_system(anim_system)
		, m_animables(allocator)
	{
		m_render_scene = nullptr;
		uint32_t hash = crc32("renderer");
		for (auto* scene : ctx.m_scenes)
		{
			if (crc32(scene->getPlugin().getName()) == hash)
			{
				m_render_scene = static_cast<RenderScene*>(scene);
				break;
			}
		}
		ASSERT(m_render_scene);
		m_render_scene->renderableCreated()
			.bind<AnimationSceneImpl, &AnimationSceneImpl::onRenderableCreated>(
				this);
		m_render_scene->renderableDestroyed()
			.bind<AnimationSceneImpl,
				  &AnimationSceneImpl::onRenderableDestroyed>(this);
	}


	~AnimationSceneImpl()
	{
		m_render_scene->renderableCreated()
			.unbind<AnimationSceneImpl,
					&AnimationSceneImpl::onRenderableCreated>(this);
		m_render_scene->renderableDestroyed()
			.unbind<AnimationSceneImpl,
					&AnimationSceneImpl::onRenderableDestroyed>(this);
	}


	virtual Universe& getUniverse() override { return m_universe; }


	virtual bool ownComponentType(uint32_t type) const override
	{
		return type == ANIMABLE_HASH;
	}


	virtual ComponentIndex createComponent(uint32_t type,
										   Entity entity) override
	{
		if (type == ANIMABLE_HASH)
		{
			return createAnimable(entity);
		}
		return INVALID_COMPONENT;
	}


	virtual void destroyComponent(ComponentIndex component, uint32_t type)
	{
		if (type == ANIMABLE_HASH)
		{
			m_animables[component].m_is_free = true;
			m_universe.destroyComponent(
				m_animables[component].m_entity, type, this, component);
		}
	}


	virtual void serialize(OutputBlob& serializer) override
	{
		serializer.write((int32_t)m_animables.size());
		for (int i = 0; i < m_animables.size(); ++i)
		{
			serializer.write(m_animables[i].m_entity);
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
			serializer.read(m_animables[i].m_entity);
			ComponentIndex renderable =
				m_render_scene->getRenderableComponent(m_animables[i].m_entity);
			if (renderable >= 0)
			{
				m_animables[i].m_renderable = renderable;
			}
			serializer.read(m_animables[i].m_time);
			serializer.read(m_animables[i].m_is_free);
			char path[MAX_PATH_LENGTH];
			serializer.readString(path, sizeof(path));
			m_animables[i].m_animation =
				path[0] == '\0' ? nullptr : loadAnimation(path);
			m_universe.addComponent(
				m_animables[i].m_entity, ANIMABLE_HASH, this, i);
		}
	}


	const char* getPreview(ComponentIndex cmp)
	{
		return m_animables[cmp].m_animation
				   ? m_animables[cmp].m_animation->getPath().c_str()
				   : "";
	}


	void setPreview(ComponentIndex cmp, const char* path)
	{
		playAnimation(cmp, path);
	}


	void playAnimation(ComponentIndex cmp, const char* path)
	{
		m_animables[cmp].m_animation = loadAnimation(path);
		m_animables[cmp].m_time = 0;
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
				animable.m_animation->getPose(
					animable.m_time,
					m_render_scene->getPose(animable.m_renderable),
					*m_render_scene->getRenderableModel(animable.m_renderable));
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


private:
	Animation* loadAnimation(const char* path)
	{
		ResourceManager& rm = m_engine.getResourceManager();
		return static_cast<Animation*>(
			rm.get(ResourceManager::ANIMATION)->load(Path(path)));
	}


	void onRenderableCreated(ComponentIndex cmp)
	{
		Entity entity = m_render_scene->getRenderableEntity(cmp);
		for (int i = 0; i < m_animables.size(); ++i)
		{
			if (m_animables[i].m_entity == entity)
			{
				m_animables[i].m_renderable = cmp;
				break;
			}
		}
	}


	void onRenderableDestroyed(ComponentIndex cmp)
	{
		Entity entity = m_render_scene->getRenderableEntity(cmp);
		for (int i = 0; i < m_animables.size(); ++i)
		{
			if (m_animables[i].m_entity == entity)
			{
				m_animables[i].m_renderable = INVALID_COMPONENT;
				break;
			}
		}
	}


	ComponentIndex createAnimable(Entity entity)
	{
		Animable* src = nullptr;
		for (int i = 0, c = m_animables.size(); i < c; ++i)
		{
			if (m_animables[i].m_is_free)
			{
				src = &m_animables[i];
				break;
			}
		}
		Animable& animable = src ? *src : m_animables.pushEmpty();
		animable.m_time = 0;
		animable.m_is_free = false;
		animable.m_renderable = INVALID_COMPONENT;
		animable.m_animation = nullptr;
		animable.m_entity = entity;

		ComponentIndex renderable =
			m_render_scene->getRenderableComponent(entity);
		if (renderable >= 0)
		{
			animable.m_renderable = renderable;
		}

		m_universe.addComponent(
			entity, ANIMABLE_HASH, this, m_animables.size() - 1);
		return m_animables.size() - 1;
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

	virtual IScene* createScene(UniverseContext& ctx) override
	{
		return m_allocator.newObject<AnimationSceneImpl>(
			*this, m_engine, ctx, m_allocator);
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
