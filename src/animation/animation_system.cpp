#include "animation_system.h"
#include "animation/animation.h"
#include "core/base_proxy_allocator.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "editor/asset_browser.h"
#include "editor/imgui/imgui.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "renderer/render_scene.h"
#include "universe/universe.h"


namespace Lumix
{

static const uint32 RENDERABLE_HASH = crc32("renderable");
static const uint32 ANIMABLE_HASH = crc32("animable");

namespace FS
{
class FileSystem;
};

class Animation;
class Engine;
class JsonSerializer;
class Universe;


struct AnimationSceneImpl : public IScene
{
	struct Animable
	{
		bool m_is_free;
		ComponentIndex m_renderable;
		float m_time;
		class Animation* m_animation;
		Entity m_entity;
	};


	AnimationSceneImpl(IPlugin& anim_system, Engine& engine, Universe& ctx, IAllocator& allocator)
		: m_universe(ctx)
		, m_engine(engine)
		, m_anim_system(anim_system)
		, m_animables(allocator)
	{
		m_is_game_running = false;
		m_render_scene = nullptr;
		uint32 hash = crc32("renderer");
		for (auto* scene : ctx.getScenes())
		{
			if (crc32(scene->getPlugin().getName()) == hash)
			{
				m_render_scene = static_cast<RenderScene*>(scene);
				break;
			}
		}
		ASSERT(m_render_scene);
		m_render_scene->renderableCreated()
			.bind<AnimationSceneImpl, &AnimationSceneImpl::onRenderableCreated>(this);
		m_render_scene->renderableDestroyed()
			.bind<AnimationSceneImpl, &AnimationSceneImpl::onRenderableDestroyed>(this);
	}


	~AnimationSceneImpl()
	{
		for(auto& animable : m_animables)
		{
			if(animable.m_is_free) continue;
			unloadAnimation(animable.m_animation);
		}

		m_render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (m_render_scene)
		{
			m_render_scene->renderableCreated()
				.unbind<AnimationSceneImpl, &AnimationSceneImpl::onRenderableCreated>(this);
			m_render_scene->renderableDestroyed()
				.unbind<AnimationSceneImpl, &AnimationSceneImpl::onRenderableDestroyed>(this);
		}
	}


	void startGame() override 
	{
		m_is_game_running = true;
	}


	void stopGame() override
	{
		m_is_game_running = false;
	}



	Universe& getUniverse() override { return m_universe; }


	ComponentIndex getComponent(Entity entity, uint32 type) override
	{
		ASSERT(ownComponentType(type));
		for (int i = 0; i < m_animables.size(); ++i)
		{
			if (!m_animables[i].m_is_free && m_animables[i].m_entity == entity) return i;
		}
		return INVALID_COMPONENT;
	}


	bool ownComponentType(uint32 type) const override
	{
		return type == ANIMABLE_HASH;
	}


	ComponentIndex createComponent(uint32 type,
										   Entity entity) override
	{
		if (type == ANIMABLE_HASH)
		{
			return createAnimable(entity);
		}
		return INVALID_COMPONENT;
	}


	void unloadAnimation(Animation* animation)
	{
		if(!animation) return;

		auto& rm = animation->getResourceManager();
		auto* animation_manager = rm.get(ResourceManager::ANIMATION);
		animation_manager->unload(*animation);
	}


	void destroyComponent(ComponentIndex component, uint32 type) override
	{
		if (type == ANIMABLE_HASH)
		{
			unloadAnimation(m_animables[component].m_animation);
			m_animables[component].m_is_free = true;
			m_universe.destroyComponent(m_animables[component].m_entity, type, this, component);
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write((int32)m_animables.size());
		for (int i = 0; i < m_animables.size(); ++i)
		{
			serializer.write(m_animables[i].m_entity);
			serializer.write(m_animables[i].m_time);
			serializer.write(m_animables[i].m_is_free);
			serializer.writeString(
				m_animables[i].m_animation ? m_animables[i].m_animation->getPath().c_str() : "");
		}
	}


	void deserialize(InputBlob& serializer, int) override
	{
		int32 count;
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
			m_animables[i].m_animation = path[0] == '\0' ? nullptr : loadAnimation(Path(path));
			m_universe.addComponent(m_animables[i].m_entity, ANIMABLE_HASH, this, i);
		}
	}


	Path getAnimation(ComponentIndex cmp)
	{
		return m_animables[cmp].m_animation
				   ? m_animables[cmp].m_animation->getPath()
				   : Path("");
	}


	void setAnimation(ComponentIndex cmp, const Path& path)
	{
		unloadAnimation(m_animables[cmp].m_animation);
		m_animables[cmp].m_animation = loadAnimation(path);
		m_animables[cmp].m_time = 0;
	}


	void updateAnimable(ComponentIndex cmp, float time_delta)
	{
		Animable& animable = m_animables[cmp];
		if(!animable.m_is_free && animable.m_animation &&
			animable.m_animation->isReady())
		{
			auto* pose = m_render_scene->getPose(animable.m_renderable);
			if(!pose) return;
			animable.m_animation->getPose(
				animable.m_time,
				*pose,
				*m_render_scene->getRenderableModel(animable.m_renderable));
			float t = animable.m_time + time_delta;
			float l = animable.m_animation->getLength();
			while(t > l)
			{
				t -= l;
			}
			animable.m_time = t;
		}
	}


	void update(float time_delta, bool paused) override
	{
		PROFILE_FUNCTION();
		if (m_animables.empty()) return;
		if (!m_is_game_running) return;

		for (int i = 0, c = m_animables.size(); i < c; ++i)
		{
			updateAnimable(i, time_delta);
		}
	}


	Animation* loadAnimation(const Path& path)
	{
		ResourceManager& rm = m_engine.getResourceManager();
		return static_cast<Animation*>(rm.get(ResourceManager::ANIMATION)->load(path));
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
		Animable& animable = src ? *src : m_animables.emplace();
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
public:
	AnimationSystemImpl(Engine& engine)
		: m_allocator(engine.getAllocator())
		, m_engine(engine)
		, m_animation_manager(m_allocator)
	{
		PropertyRegister::registerComponentType("animable", "Animable");

		PropertyRegister::add("animable", LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationSceneImpl>)("Animation",
			&AnimationSceneImpl::getAnimation,
			&AnimationSceneImpl::setAnimation,
			"Animation (*.ani)",
			ResourceManager::ANIMATION,
			m_allocator));
	}

	IScene* createScene(Universe& ctx) override
	{
		return LUMIX_NEW(m_allocator, AnimationSceneImpl)(
			*this, m_engine, ctx, m_allocator);
	}


	void destroyScene(IScene* scene) override
	{
		LUMIX_DELETE(m_allocator, scene);
	}


	const char* getName() const override { return "animation"; }


	bool create() override
	{
		m_animation_manager.create(ResourceManager::ANIMATION,
								   m_engine.getResourceManager());
		return true;
	}


	void destroy() override {}

	Lumix::IAllocator& m_allocator;
	Engine& m_engine;
	AnimationManager m_animation_manager;

private:
	void operator=(const AnimationSystemImpl&);
	AnimationSystemImpl(const AnimationSystemImpl&);
};


struct AssetBrowserPlugin : AssetBrowser::IPlugin
{

	AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
	{}


	bool onGUI(Lumix::Resource* resource, Lumix::uint32 type) override
	{
		if(type == ResourceManager::ANIMATION)
		{
			auto* animation = static_cast<Animation*>(resource);
			ImGui::LabelText("FPS", "%d", animation->getFPS());
			ImGui::LabelText("Length", "%.3fs", animation->getLength());
			ImGui::LabelText("Frames", "%d", animation->getFrameCount());

			return true;
		}
		return false;
	}


	void onResourceUnloaded(Resource* resource) override
	{
	}


	const char* getName() const override
	{
		return "Animation";
	}


	bool hasResourceManager(uint32 type) const override
	{
		return type == ResourceManager::ANIMATION;
	}


	uint32 getResourceType(const char* ext) override
	{
		if(compareString(ext, "ani") == 0) return ResourceManager::ANIMATION;
		return 0;
	}


	StudioApp& m_app;

};


struct PropertyGridPlugin : PropertyGrid::IPlugin
{
	PropertyGridPlugin(StudioApp& app)
		: m_app(app)
	{
		m_is_playing = false;
		m_time_scale = 1;
	}


	void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) override
	{
		if(cmp.type != ANIMABLE_HASH) return;

		auto* scene = static_cast<AnimationSceneImpl*>(cmp.scene);
		auto& animable = scene->m_animables[cmp.index];
		if(!animable.m_animation) return;
		if(!animable.m_animation->isReady()) return;

		ImGui::Checkbox("Play", &m_is_playing);
		if(ImGui::SliderFloat("Time", &animable.m_time, 0, animable.m_animation->getLength()))
		{
			scene->updateAnimable(cmp.index, 0);
		}

		if(m_is_playing)
		{
			ImGui::InputFloat("Time scale", &m_time_scale, 0.1f, 1.0f);
			m_time_scale = Math::maxValue(0.0f, m_time_scale);
			float time_delta = m_app.getWorldEditor()->getEngine().getLastTimeDelta();
			scene->updateAnimable(cmp.index, time_delta * m_time_scale);
		}

	}


	StudioApp& m_app;
	bool m_is_playing;
	float m_time_scale;
};


extern "C" LUMIX_ANIMATION_API void setStudioApp(StudioApp& app)
{
	auto& allocator = app.getWorldEditor()->getAllocator();
	auto* ab_plugin = LUMIX_NEW(allocator, AssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*ab_plugin);

	auto* pg_plugin = LUMIX_NEW(allocator, PropertyGridPlugin)(app);
	app.getPropertyGrid()->addPlugin(*pg_plugin);
}


extern "C" IPlugin* createPlugin(Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), AnimationSystemImpl)(engine);
}


} // ~namespace Lumix
