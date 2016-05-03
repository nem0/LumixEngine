#include "animation_system.h"
#include "animation/animation.h"
#include "engine/core/base_proxy_allocator.h"
#include "engine/core/blob.h"
#include "engine/core/crc32.h"
#include "engine/core/json_serializer.h"
#include "engine/core/profiler.h"
#include "engine/core/resource_manager.h"
#include "editor/asset_browser.h"
#include "editor/imgui/imgui.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_scene.h"
#include "engine/universe/universe.h"


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


enum class AnimationSceneVersion : int
{
	FIRST,

	LATEST
};


struct AnimationSceneImpl : public IScene
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

	
	void startGame() override { m_is_game_running = true; }
	void stopGame() override { m_is_game_running = false; }
	Universe& getUniverse() override { return m_universe; }


	ComponentIndex getComponent(Entity entity, uint32 type) override
	{
		ASSERT(ownComponentType(type));
		for (int i = 0; i < m_animables.size(); ++i)
		{
			if ((m_animables[i].flags & Animable::FREE) == 0 && m_animables[i].entity == entity) return i;
		}
		return INVALID_COMPONENT;
	}


	bool ownComponentType(uint32 type) const override { return type == ANIMABLE_HASH; }


	ComponentIndex createComponent(uint32 type, Entity entity) override
	{
		if (type == ANIMABLE_HASH) return createAnimable(entity);
		return INVALID_COMPONENT;
	}


	void unloadAnimation(Animation* animation)
	{
		if (!animation) return;

		auto& rm = animation->getResourceManager();
		auto* animation_manager = rm.get(ResourceManager::ANIMATION);
		animation_manager->unload(*animation);
	}


	void destroyComponent(ComponentIndex component, uint32 type) override
	{
		if (type == ANIMABLE_HASH)
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
				m_universe.addComponent(m_animables[i].entity, ANIMABLE_HASH, this, i);
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


	void updateAnimable(ComponentIndex cmp, float time_delta)
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
			updateAnimable(i, time_delta);
		}
	}


	Animation* loadAnimation(const Path& path)
	{
		ResourceManager& rm = m_engine.getResourceManager();
		return static_cast<Animation*>(rm.get(ResourceManager::ANIMATION)->load(path));
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

		m_universe.addComponent(entity, ANIMABLE_HASH, this, cmp);
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
		PropertyRegister::registerComponentType("animable", "Animable");

		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationSceneImpl>)("Animation",
								  &AnimationSceneImpl::getAnimation,
								  &AnimationSceneImpl::setAnimation,
								  "Animation (*.ani)",
								  ResourceManager::ANIMATION,
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
		animation_manager.create(ResourceManager::ANIMATION, m_engine.getResourceManager());
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


namespace
{


struct AssetBrowserPlugin : AssetBrowser::IPlugin
{

	explicit AssetBrowserPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool onGUI(Lumix::Resource* resource, Lumix::uint32 type) override
	{
		if (type == ResourceManager::ANIMATION)
		{
			auto* animation = static_cast<Animation*>(resource);
			ImGui::LabelText("FPS", "%d", animation->getFPS());
			ImGui::LabelText("Length", "%.3fs", animation->getLength());
			ImGui::LabelText("Frames", "%d", animation->getFrameCount());

			return true;
		}
		return false;
	}


	void onResourceUnloaded(Resource* resource) override {}


	const char* getName() const override { return "Animation"; }


	bool hasResourceManager(uint32 type) const override { return type == ResourceManager::ANIMATION; }


	uint32 getResourceType(const char* ext) override
	{
		if (compareString(ext, "ani") == 0) return ResourceManager::ANIMATION;
		return 0;
	}


	StudioApp& m_app;
};


struct PropertyGridPlugin : PropertyGrid::IPlugin
{
	explicit PropertyGridPlugin(StudioApp& app)
		: m_app(app)
	{
		m_is_playing = false;
	}


	void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) override
	{
		if (cmp.type != ANIMABLE_HASH) return;

		auto* scene = static_cast<AnimationSceneImpl*>(cmp.scene);
		auto& animable = scene->m_animables[cmp.index];
		if (!animable.animation) return;
		if (!animable.animation->isReady()) return;

		ImGui::Checkbox("Preview", &m_is_playing);
		if (ImGui::SliderFloat("Time", &animable.time, 0, animable.animation->getLength()))
		{
			scene->updateAnimable(cmp.index, 0);
		}

		if (m_is_playing)
		{
			float time_delta = m_app.getWorldEditor()->getEngine().getLastTimeDelta();
			scene->updateAnimable(cmp.index, time_delta);
		}
	}


	StudioApp& m_app;
	bool m_is_playing;
};


} // anonymous namespace


LUMIX_STUDIO_ENTRY(animation)
{
	auto& allocator = app.getWorldEditor()->getAllocator();
	auto* ab_plugin = LUMIX_NEW(allocator, AssetBrowserPlugin)(app);
	app.getAssetBrowser()->addPlugin(*ab_plugin);

	auto* pg_plugin = LUMIX_NEW(allocator, PropertyGridPlugin)(app);
	app.getPropertyGrid()->addPlugin(*pg_plugin);
}


LUMIX_PLUGIN_ENTRY(animation)
{
	return LUMIX_NEW(engine.getAllocator(), AnimationSystemImpl)(engine);
}
}
