#include "animation_system.h"
#include "animation/animation.h"
#include "animation/controller.h"
#include "animation/controller.h"
#include "engine/base_proxy_allocator.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/json_serializer.h"
#include "engine/lua_wrapper.h"
#include "engine/profiler.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_scene.h"
#include <cfloat>


namespace Lumix
{


static const ComponentType ANIMABLE_TYPE = PropertyRegister::getComponentType("animable");
static const ComponentType CONTROLER_TYPE = PropertyRegister::getComponentType("anim_controller");
static const ResourceType ANIMATION_TYPE("animation");
static const ResourceType CONTROLLER_RESOURCE_TYPE("anim_controller");


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


struct AnimationSceneImpl LUMIX_FINAL : public AnimationScene
{
	friend struct AnimationSystemImpl;

	struct Controller
	{
		Anim::ControllerResource* resource = nullptr;
	};


	struct Animable
	{
		float time;
		float time_scale;
		float start_time;
		Animation* animation;
		Entity entity;
	};


	struct Mixer
	{
		struct Input
		{
			Animation* animation = nullptr;
			float time = 0.0f;
			float weight = 0.0f;
		};

		Input inputs[8];
		Entity entity;
	};

	AnimationSceneImpl(IPlugin& anim_system, Engine& engine, Universe& universe, IAllocator& allocator)
		: m_universe(universe)
		, m_engine(engine)
		, m_anim_system(anim_system)
		, m_animables(allocator)
		, m_controllers(allocator)
		, m_mixers(allocator)
	{
		m_universe.entityDestroyed().bind<AnimationSceneImpl, &AnimationSceneImpl::onEntityDestroyed>(this);
		m_is_game_running = false;
		m_render_scene = static_cast<RenderScene*>(universe.getScene(crc32("renderer")));
		universe.registerComponentTypeScene(ANIMABLE_TYPE, this);
		universe.registerComponentTypeScene(CONTROLER_TYPE, this);
		ASSERT(m_render_scene);
	}


	~AnimationSceneImpl()
	{
		m_universe.entityDestroyed().unbind<AnimationSceneImpl, &AnimationSceneImpl::onEntityDestroyed>(this);
	}


	void onEntityDestroyed(Entity entity)
	{
		m_mixers.erase(entity);
	}


	void clear() override
	{
		for (Mixer& mixer : m_mixers)
		{
			for (auto& input : mixer.inputs)
			{
				unloadAnimation(input.animation);
			}
		}
		m_mixers.clear();

		for (Animable& animable : m_animables)
		{
			unloadAnimation(animable.animation);
		}
		m_animables.clear();
	}


	float getAnimationLength(int animation_idx)
	{
		auto* animation = static_cast<Animation*>(animation_idx > 0 ? m_engine.getLuaResource(animation_idx) : nullptr);
		if (animation) return animation->getLength();
		return 0;
	}


	void mixAnimation(Entity entity, int animation_idx, int input_idx, float time, float weight)
	{
		auto* animation = static_cast<Animation*>(animation_idx > 0 ? m_engine.getLuaResource(animation_idx) : nullptr);
		int mixer_idx = m_mixers.find(entity);
		if (mixer_idx < 0)
		{
			Mixer& mixer = m_mixers.insert(entity);
			mixer.entity = entity;
			mixer_idx = m_mixers.find(entity);
		}
		Mixer& mixer = m_mixers.at(mixer_idx);
		auto& input = mixer.inputs[input_idx];
		input.animation = animation;
		input.time = time;
		input.weight = weight;
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
		else if (type == CONTROLER_TYPE)
		{
			if (m_controllers.find(entity) < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		return INVALID_COMPONENT;
	}


	ComponentHandle createComponent(ComponentType type, Entity entity) override
	{
		if (type == ANIMABLE_TYPE) return createAnimable(entity);
		if (type == CONTROLER_TYPE) return createController(entity);
		return INVALID_COMPONENT;
	}


	void unloadAnimation(Animation* animation)
	{
		if (!animation) return;

		animation->getResourceManager().unload(*animation);
	}


	void unloadController(Anim::ControllerResource* res)
	{
		if (!res) return;

		res->getResourceManager().unload(*res);
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
		else if (type == CONTROLER_TYPE)
		{
			Entity entity = {component.index};
			auto& controller = m_controllers[entity];
			unloadController(controller.resource);
			m_controllers.erase(entity);
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
		m_animables.reserve(count);
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


	void setControllerSource(ComponentHandle cmp, const Path& path)
	{
		auto& controller = m_controllers[{cmp.index}];
		unloadController(controller.resource);
		controller.resource = loadController(path);
	}


	Path getControllerSource(ComponentHandle cmp)
	{
		const auto& controller = m_controllers[{cmp.index}];
		return controller.resource ? controller.resource->getPath() : Path("");
	}


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


	void updateMixer(Mixer& mixer, float time_delta)
	{
		ComponentHandle model_instance = m_render_scene->getModelInstanceComponent(mixer.entity);
		if (model_instance == INVALID_COMPONENT) return;

		auto* pose = m_render_scene->getPose(model_instance);
		auto* model = m_render_scene->getModelInstanceModel(model_instance);

		if (!pose) return;
		if (!model->isReady()) return;

		model->getPose(*pose);
		pose->computeRelative(*model);

		for (int i = 0; i < lengthOf(mixer.inputs); ++i)
		{
			Mixer::Input& input = mixer.inputs[i];
			if (!input.animation || !input.animation->isReady()) break;
			if (i == 0)
			{
				input.animation->getRelativePose(input.time, *pose, *model);
			}
			else
			{
				input.animation->getRelativePose(input.time, *pose, *model, input.weight);
			}
			input.animation = nullptr;
		}
		pose->computeAbsolute(*model);
	}


	void updateAnimable(Animable& animable, float time_delta)
	{
		if (!animable.animation || !animable.animation->isReady()) return;
		ComponentHandle model_instance = m_render_scene->getModelInstanceComponent(animable.entity);
		if (model_instance == INVALID_COMPONENT) return;

		auto* pose = m_render_scene->getPose(model_instance);
		auto* model = m_render_scene->getModelInstanceModel(model_instance);

		if (!pose) return;
		if (!model->isReady()) return;

		model->getPose(*pose);
		pose->computeRelative(*model);
		animable.animation->getRelativePose(animable.time, *pose, *model);
		pose->computeAbsolute(*model);

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

		for (Mixer& mixer : m_mixers)
		{
			AnimationSceneImpl::updateMixer(mixer, time_delta);
		}

		for (Animable& animable : m_animables)
		{
			AnimationSceneImpl::updateAnimable(animable, time_delta);
		}
	}


	Animation* loadAnimation(const Path& path)
	{
		ResourceManager& rm = m_engine.getResourceManager();
		return static_cast<Animation*>(rm.get(ANIMATION_TYPE)->load(path));
	}


	Anim::ControllerResource* loadController(const Path& path)
	{
		ResourceManager& rm = m_engine.getResourceManager();
		return static_cast<Anim::ControllerResource*>(rm.get(CONTROLLER_RESOURCE_TYPE)->load(path));
	}


	ComponentHandle createAnimable(Entity entity)
	{
		Animable& animable = m_animables.insert(entity);
		animable.time = 0;
		animable.animation = nullptr;
		animable.entity = entity;
		animable.time_scale = 1;
		animable.start_time = 0;

		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, ANIMABLE_TYPE, this, cmp);
		return cmp;
	}


	ComponentHandle createController(Entity entity)
	{
		Controller& controller = m_controllers.insert(entity);

		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, CONTROLER_TYPE, this, cmp);
		return cmp;
	}

	IPlugin& getPlugin() const override { return m_anim_system; }


	Universe& m_universe;
	IPlugin& m_anim_system;
	Engine& m_engine;
	AssociativeArray<Entity, Animable> m_animables;
	AssociativeArray<Entity, Controller> m_controllers;
	AssociativeArray<Entity, Mixer> m_mixers;
	RenderScene* m_render_scene;
	bool m_is_game_running;
};


struct AnimationSystemImpl LUMIX_FINAL : public IPlugin
{
	explicit AnimationSystemImpl(Engine& engine)
		: m_allocator(engine.getAllocator())
		, m_engine(engine)
		, m_animation_manager(m_allocator)
		, m_controller_manager(m_allocator)
	{
		m_animation_manager.create(ANIMATION_TYPE, m_engine.getResourceManager());
		m_controller_manager.create(CONTROLLER_RESOURCE_TYPE, m_engine.getResourceManager());

		PropertyRegister::add("anim_controller",
			LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationSceneImpl>)("Source",
				&AnimationSceneImpl::getControllerSource,
				&AnimationSceneImpl::setControllerSource,
				"Animation controller (*.act)",
				CONTROLLER_RESOURCE_TYPE));

		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationSceneImpl>)("Animation",
				&AnimationSceneImpl::getAnimation,
				&AnimationSceneImpl::setAnimation,
				"Animation (*.ani)",
				ANIMATION_TYPE));
		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationSceneImpl>)(
				"Start time", &AnimationSceneImpl::getStartTime, &AnimationSceneImpl::setStartTime, 0, FLT_MAX, 0.1f));
		PropertyRegister::add("animable",
			LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationSceneImpl>)(
				"Time scale", &AnimationSceneImpl::getTimeScale, &AnimationSceneImpl::setTimeScale, 0, FLT_MAX, 0.1f));

		registerLuaAPI();
	}


	~AnimationSystemImpl()
	{
		m_animation_manager.destroy();
		m_controller_manager.destroy();
	}


	void registerLuaAPI()
	{
		lua_State* L = m_engine.getState();
		#define REGISTER_FUNCTION(name) \
		do {\
			auto f = &LuaWrapper::wrapMethod<AnimationSceneImpl, decltype(&AnimationSceneImpl::name), &AnimationSceneImpl::name>; \
			LuaWrapper::createSystemFunction(L, "Animation", #name, f); \
		} while(false) \

		REGISTER_FUNCTION(mixAnimation);
		REGISTER_FUNCTION(getAnimationLength);

		#undef REGISTER_FUNCTION
	}


	void createScenes(Universe& ctx) override
	{
		auto* scene = LUMIX_NEW(m_allocator, AnimationSceneImpl)(*this, m_engine, ctx, m_allocator);
		ctx.addScene(scene);
	}


	void destroyScene(IScene* scene) override { LUMIX_DELETE(m_allocator, scene); }


	const char* getName() const override { return "animation"; }


	Lumix::IAllocator& m_allocator;
	Engine& m_engine;
	AnimationManager m_animation_manager;
	Anim::ControllerManager m_controller_manager;

private:
	void operator=(const AnimationSystemImpl&);
	AnimationSystemImpl(const AnimationSystemImpl&);
};


LUMIX_PLUGIN_ENTRY(animation)
{
	return LUMIX_NEW(engine.getAllocator(), AnimationSystemImpl)(engine);
}
}
