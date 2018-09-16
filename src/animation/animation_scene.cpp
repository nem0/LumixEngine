#include "animation_scene.h"

#include "animation/animation.h"
#include "animation/controller.h"
#include "animation/events.h"
#include "animation/property_animation.h"
#include "engine/base_proxy_allocator.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/lua_wrapper.h"
#include "engine/job_system.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
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
	PROPERTY_ANIMATOR,

	LATEST
};


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
static const ComponentType ANIMABLE_TYPE = Reflection::getComponentType("animable");
static const ComponentType PROPERTY_ANIMATOR_TYPE = Reflection::getComponentType("property_animator");
static const ComponentType CONTROLLER_TYPE = Reflection::getComponentType("anim_controller");
static const ComponentType SHARED_CONTROLLER_TYPE = Reflection::getComponentType("shared_anim_controller");


namespace FS
{
class FileSystem;
}


class Animation;
class Engine;
class JsonSerializer;
class Universe;


struct AnimationSceneImpl final : public AnimationScene
{
	friend struct AnimationSystemImpl;

	struct SharedController
	{
		EntityRef entity;
		EntityPtr parent;
	};

	struct Controller
	{
		explicit Controller(IAllocator& allocator) : input(allocator), animations(allocator) {}

		EntityRef entity;
		Anim::ControllerResource* resource = nullptr;
		Anim::ComponentInstance* root = nullptr;
		u32 default_set = 0;
		Array<u8> input;
		HashMap<u32, Animation*> animations;

		struct IK
		{
			enum { MAX_BONES_COUNT = 8 };
			float weight = 0;
			i16 max_iterations = 5;
			i16 bones_count = 4;
			u32 bones[MAX_BONES_COUNT];
			DVec3 target;
		} inverse_kinematics[4];
	};


	struct PropertyAnimator
	{
		struct Key
		{
			int frame0;
			int frame1;
			float value0;
			float value1;
		};

		enum Flags
		{
			LOOPED = 1 << 0,
			DISABLED = 1 << 1
		};

		PropertyAnimator(IAllocator& allocator) : keys(allocator) {}

		PropertyAnimation* animation;
		Array<Key> keys;

		FlagSet<Flags, u32> flags;
		float time;
	};


	struct Animable
	{
		float time;
		float time_scale;
		float start_time;
		Animation* animation;
		EntityRef entity;
	};


	AnimationSceneImpl(Engine& engine, IPlugin& anim_system, Universe& universe, IAllocator& allocator)
		: m_universe(universe)
		, m_engine(engine)
		, m_anim_system(anim_system)
		, m_animables(allocator)
		, m_property_animators(allocator)
		, m_controllers(allocator)
		, m_shared_controllers(allocator)
		, m_event_stream(allocator)
		, m_allocator(allocator)
	{
		m_is_game_running = false;
		m_render_scene = static_cast<RenderScene*>(universe.getScene(crc32("renderer")));
		universe.registerComponentType(PROPERTY_ANIMATOR_TYPE
			, this
			, &AnimationSceneImpl::createPropertyAnimator
			, &AnimationSceneImpl::destroyPropertyAnimator
			, &AnimationSceneImpl::serializePropertyAnimator
			, &AnimationSceneImpl::deserializePropertyAnimator);
		universe.registerComponentType(ANIMABLE_TYPE
			, this
			, &AnimationSceneImpl::createAnimable
			, &AnimationSceneImpl::destroyAnimable
			, &AnimationSceneImpl::serializeAnimable
			, &AnimationSceneImpl::deserializeAnimable);
		universe.registerComponentType(CONTROLLER_TYPE
			, this
			, &AnimationSceneImpl::createController
			, &AnimationSceneImpl::destroyController
			, &AnimationSceneImpl::serializeController
			, &AnimationSceneImpl::deserializeController);
		universe.registerComponentType(SHARED_CONTROLLER_TYPE
			, this
			, &AnimationSceneImpl::createSharedController
			, &AnimationSceneImpl::destroySharedController
			, &AnimationSceneImpl::serializeSharedController
			, &AnimationSceneImpl::deserializeSharedController);
		ASSERT(m_render_scene);
	}


	void serializeSharedController(ISerializer& serializer, EntityRef entity)
	{
		SharedController& ctrl = m_shared_controllers[entity];
		serializer.write("parent", ctrl.parent);
	}


	void deserializeSharedController(IDeserializer& serializer, EntityRef entity, int /*scene_version*/)
	{
		EntityRef parent;
		serializer.read(&parent);
		m_shared_controllers.insert(entity, {entity, parent});
		m_universe.onComponentCreated(entity, SHARED_CONTROLLER_TYPE, this);
	}


	void serializePropertyAnimator(ISerializer& serializer, EntityRef entity)
	{
		int idx = m_property_animators.find(entity);
		PropertyAnimator& animator = m_property_animators.at(idx);
		serializer.write("animation", animator.animation ? animator.animation->getPath().c_str() : "");
		serializer.write("flags", animator.flags.base);
	}
		

	void deserializePropertyAnimator(IDeserializer& serializer, EntityRef entity, int /*scene_version*/)
	{
		PropertyAnimator& animator = m_property_animators.emplace(entity, m_allocator);
		animator.time = 0;
		char tmp[MAX_PATH_LENGTH];
		serializer.read(tmp, lengthOf(tmp));
		animator.animation = loadPropertyAnimation(Path(tmp));
		serializer.read(&animator.flags.base);
		m_universe.onComponentCreated(entity, PROPERTY_ANIMATOR_TYPE, this);
	}


	void serializeAnimable(ISerializer& serializer, EntityRef entity)
	{
		Animable& animable = m_animables[entity];
		serializer.write("time_scale", animable.time_scale);
		serializer.write("start_time", animable.start_time);
		serializer.write("animation", animable.animation ? animable.animation->getPath().c_str() : "");
	}


	void deserializeAnimable(IDeserializer& serializer, EntityRef entity, int /*scene_version*/)
	{
		Animable& animable = m_animables.insert(entity);
		animable.entity = entity;
		serializer.read(&animable.time_scale);
		serializer.read(&animable.start_time);
		char tmp[MAX_PATH_LENGTH];
		serializer.read(tmp, lengthOf(tmp));
		auto* res = tmp[0] ? m_engine.getResourceManager().load<Animation>(Path(tmp)) : nullptr;
		animable.animation = (Animation*)res;
		m_universe.onComponentCreated(entity, ANIMABLE_TYPE, this);
	}


	void serializeController(ISerializer& serializer, EntityRef entity)
	{
		Controller& controller = m_controllers.get(entity);
		serializer.write("source", controller.resource ? controller.resource->getPath().c_str() : "");
		serializer.write("default_set", controller.default_set);
	}


	int getVersion() const override { return (int)AnimationSceneVersion::LATEST; }


	void deserializeController(IDeserializer& serializer, EntityRef entity, int scene_version)
	{
		Controller& controller = m_controllers.emplace(entity, m_allocator);
		controller.entity = entity;
		char tmp[MAX_PATH_LENGTH];
		serializer.read(tmp, lengthOf(tmp));
		if (scene_version > (int)AnimationSceneVersion::SHARED_CONTROLLER)
		{
			serializer.read(&controller.default_set);
		}
		auto* res = tmp[0] ? m_engine.getResourceManager().load<Anim::ControllerResource>(Path(tmp)) : nullptr;
		setControllerResource(controller, (Anim::ControllerResource*)res);
		m_universe.onComponentCreated(entity, CONTROLLER_TYPE, this);
	}


	const OutputBlob& getEventStream() const override
	{
		return m_event_stream;
	}


	void clear() override
	{
		for (PropertyAnimator& anim : m_property_animators)
		{
			unloadResource(anim.animation);
		}
		m_property_animators.clear();

		for (Animable& animable : m_animables)
		{
			unloadResource(animable.animation);
		}
		m_animables.clear();

		for (Controller& controller : m_controllers)
		{
			unloadResource(controller.resource);
			setControllerResource(controller, nullptr);
		}
		m_controllers.clear();
	}


	static int setIK(lua_State* L)
	{
		/*AnimationSceneImpl* scene = LuaWrapper::checkArg<AnimationSceneImpl*>(L, 1);
		EntityRef entity = LuaWrapper::checkArg<EntityRef>(L, 2);
		Controller& controller = scene->m_controllers.get(entity);
		int index = LuaWrapper::checkArg<int>(L, 3);
		Controller::IK& ik = controller.inverse_kinematics[index];
		ik.weight = LuaWrapper::checkArg<float>(L, 4);
		ik.target = LuaWrapper::checkArg<DVec3>(L, 5);
		Transform tr = scene->m_universe.getTransform(controller.entity);
		ik.target = tr.inverted().transform(ik.target);

		ik.bones_count = lua_gettop(L) - 5;
		if (ik.bones_count > lengthOf(ik.bones))
		{
			luaL_argerror(L, ik.bones_count, "Too many arguments");
		}
		for (int i = 0; i < ik.bones_count; ++i)
		{
			const char* bone = LuaWrapper::checkArg<const char*>(L, i + 6);
			ik.bones[i] = crc32(bone);
		}*/
		// TODO
		ASSERT(false);
		return 0;
	}


	int getControllerInputIndex(EntityRef entity, const char* name) const override
	{
		const Controller& controller = m_controllers[entity];
		Anim::InputDecl& decl = controller.resource->m_input_decl;
		for (int i = 0; i < lengthOf(decl.inputs); ++i)
		{
			if (decl.inputs[i].type != Anim::InputDecl::EMPTY && equalStrings(decl.inputs[i].name, name)) return i;
		}
		return -1;
	}


	void setControllerFloatInput(EntityRef entity, int input_idx, float value)
	{
		Controller& controller = m_controllers.get(entity);
		if (!controller.root)
		{
			g_log_warning.log("Animation") << "Trying to set input " << input_idx << " before the controller is ready";
			return;
		}
		Anim::InputDecl& decl = controller.resource->m_input_decl;
		if (input_idx < 0 || input_idx >= lengthOf(decl.inputs)) return;
		if (decl.inputs[input_idx].type == Anim::InputDecl::FLOAT)
		{
			*(float*)&controller.input[decl.inputs[input_idx].offset] = value;
		}
		else
		{
			g_log_warning.log("Animation") << "Trying to set float to " << decl.inputs[input_idx].name;
		}
	}


	void setControllerIntInput(EntityRef entity, int input_idx, int value)
	{
		Controller& controller = m_controllers.get(entity);
		if (!controller.root)
		{
			g_log_warning.log("Animation") << "Trying to set input " << input_idx << " before the controller is ready";
			return;
		}
		Anim::InputDecl& decl = controller.resource->m_input_decl;
		if (decl.inputs[input_idx].type == Anim::InputDecl::INT)
		{
			*(int*)&controller.input[decl.inputs[input_idx].offset] = value;
		}
		else
		{
			g_log_warning.log("Animation") << "Trying to set int to " << decl.inputs[input_idx].name;
		}
	}


	void setControllerBoolInput(EntityRef entity, int input_idx, bool value)
	{
		Controller& controller = m_controllers.get(entity);
		if (!controller.root)
		{
			g_log_warning.log("Animation") << "Trying to set input " << input_idx << " before the controller is ready";
			return;
		}
		Anim::InputDecl& decl = controller.resource->m_input_decl;
		if (decl.inputs[input_idx].type == Anim::InputDecl::BOOL)
		{
			*(bool*)&controller.input[decl.inputs[input_idx].offset] = value;
		}
		else
		{
			g_log_warning.log("Animation") << "Trying to set bool to " << decl.inputs[input_idx].name;
		}
	}


	float getAnimationLength(int animation_idx) override
	{
		auto* animation = static_cast<Animation*>(animation_idx > 0 ? m_engine.getLuaResource(animation_idx) : nullptr);
		if (animation) return animation->getLength();
		return 0;
	}


	float getAnimableTime(EntityRef entity) override
	{
		return m_animables[entity].time;
	}


	void setAnimableTime(EntityRef entity, float time) override
	{
		m_animables[entity].time = time;
	}


	Animation* getAnimableAnimation(EntityRef entity) override
	{
		return m_animables[entity].animation;
	}

	
	void startGame() override 
	{
		for (auto& controller : m_controllers)
		{
			initControllerRuntime(controller);
		}
		m_is_game_running = true;
	}
	
	
	void stopGame() override
	{
		for (auto& controller : m_controllers)
		{
			LUMIX_DELETE(m_allocator, controller.root);
			controller.root = nullptr;
		}
		m_is_game_running = false;
	}
	
	
	Universe& getUniverse() override { return m_universe; }


	static void unloadResource(Resource* res)
	{
		if (!res) return;

		res->getResourceManager().unload(*res);
	}


	void setControllerResource(Controller& controller, Anim::ControllerResource* res)
	{
		if (controller.resource == res) return;
		if (controller.resource != nullptr)
		{
			controller.resource->getObserverCb().unbind<AnimationSceneImpl, &AnimationSceneImpl::onControllerResourceChanged>(this);
		}
		if (controller.root != nullptr)
		{
			LUMIX_DELETE(m_engine.getAllocator(), controller.root);
			controller.root = nullptr;
			controller.default_set = 0;
			controller.animations.clear();
			controller.input.clear();
		}
		controller.resource = res;
		if (controller.resource != nullptr)
		{
			controller.resource->onLoaded<AnimationSceneImpl, &AnimationSceneImpl::onControllerResourceChanged>(this);
		}
	}


	void onControllerResourceChanged(Resource::State, Resource::State new_state, Resource& resource)
	{
		for (auto& controller : m_controllers)
		{
			if (controller.resource == &resource && controller.root != nullptr && new_state != Resource::State::READY)
			{
				LUMIX_DELETE(m_engine.getAllocator(), controller.root);
				controller.root = nullptr;
				controller.default_set = 0;
				controller.animations.clear();
				controller.input.clear();
			}
		}
	}


	void destroyPropertyAnimator(EntityRef entity)
	{
		int idx = m_property_animators.find(entity);
		auto& animator = m_property_animators.at(idx);
		unloadResource(animator.animation);
		m_property_animators.erase(entity);
		m_universe.onComponentDestroyed(entity, PROPERTY_ANIMATOR_TYPE, this);
	}


	void destroyAnimable(EntityRef entity)
	{
		auto& animable = m_animables[entity];
		unloadResource(animable.animation);
		m_animables.erase(entity);
		m_universe.onComponentDestroyed(entity, ANIMABLE_TYPE, this);
	}


	void destroyController(EntityRef entity)
	{
		auto& controller = m_controllers.get(entity);
		unloadResource(controller.resource);
		setControllerResource(controller, nullptr);
		m_controllers.erase(entity);
		m_universe.onComponentDestroyed(entity, CONTROLLER_TYPE, this);
	}


	void destroySharedController(EntityRef entity)
	{
		m_shared_controllers.erase(entity);
		m_universe.onComponentDestroyed(entity, SHARED_CONTROLLER_TYPE, this);
	}


	void serialize(OutputBlob& serializer) override
	{
		serializer.write((i32)m_animables.size());
		for (const Animable& animable : m_animables)
		{
			serializer.write(animable.entity);
			serializer.write(animable.time_scale);
			serializer.write(animable.start_time);
			serializer.writeString(animable.animation ? animable.animation->getPath().c_str() : "");
		}

		serializer.write((i32)m_property_animators.size());
		for (int i = 0, n = m_property_animators.size(); i < n; ++i)
		{
			const PropertyAnimator& animator = m_property_animators.at(i);
			EntityRef entity = m_property_animators.getKey(i);
			serializer.write(entity);
			serializer.writeString(animator.animation ? animator.animation->getPath().c_str() : "");
			serializer.write(animator.flags.base);
		}

		serializer.write(m_controllers.size());
		for (const Controller& controller : m_controllers)
		{
			serializer.write(controller.default_set);
			serializer.write(controller.entity);
			serializer.writeString(controller.resource ? controller.resource->getPath().c_str() : "");
		}

		serializer.write(m_shared_controllers.size());
		for (const SharedController& controller : m_shared_controllers)
		{
			serializer.write(controller.entity);
			serializer.write(controller.parent);
		}
	}


	void deserialize(InputBlob& serializer) override
	{
		i32 count;
		serializer.read(count);
		m_animables.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			Animable animable;
			serializer.read(animable.entity);
			serializer.read(animable.time_scale);
			serializer.read(animable.start_time);
			animable.time = animable.start_time;

			char path[MAX_PATH_LENGTH];
			serializer.readString(path, sizeof(path));
			animable.animation = path[0] == '\0' ? nullptr : loadAnimation(Path(path));
			m_animables.insert(animable.entity, animable);
			m_universe.onComponentCreated(animable.entity, ANIMABLE_TYPE, this);
		}

		serializer.read(count);
		m_property_animators.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			EntityRef entity;
			serializer.read(entity);

			PropertyAnimator& animator = m_property_animators.emplace(entity, m_allocator);
			char path[MAX_PATH_LENGTH];
			serializer.readString(path, sizeof(path));
			serializer.read(animator.flags.base);
			animator.time = 0;
			animator.animation = loadPropertyAnimation(Path(path));
			m_universe.onComponentCreated(entity, PROPERTY_ANIMATOR_TYPE, this);
		}


		serializer.read(count);
		m_controllers.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			Controller controller(m_allocator);
			serializer.read(controller.default_set);
			serializer.read(controller.entity);
			char tmp[MAX_PATH_LENGTH];
			serializer.readString(tmp, lengthOf(tmp));
			setControllerResource(controller, tmp[0] ? loadController(Path(tmp)) : nullptr);
			m_controllers.insert(controller.entity, Move(controller));
			m_universe.onComponentCreated(controller.entity, CONTROLLER_TYPE, this);
		}

		serializer.read(count);
		m_shared_controllers.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			SharedController controller;
			serializer.read(controller.entity);
			serializer.read(controller.parent);
			m_shared_controllers.insert(controller.entity, controller);
			m_universe.onComponentCreated(controller.entity, SHARED_CONTROLLER_TYPE, this);
		}
	}


	void setSharedControllerParent(EntityRef entity, EntityRef parent) override
	{
		m_shared_controllers[entity].parent = parent;
	}


	EntityPtr getSharedControllerParent(EntityRef entity) override { return m_shared_controllers[entity].parent; }


	float getAnimableTimeScale(EntityRef entity) override { return m_animables[entity].time_scale; }
	void setAnimableTimeScale(EntityRef entity, float time_scale) override { m_animables[entity].time_scale = time_scale; }
	float getAnimableStartTime(EntityRef entity) override { return m_animables[entity].start_time; }
	void setAnimableStartTime(EntityRef entity, float time) override { m_animables[entity].start_time = time; }


	void setControllerSource(EntityRef entity, const Path& path) override
	{
		auto& controller = m_controllers.get(entity);
		unloadResource(controller.resource);
		setControllerResource(controller, loadController(path));
		if (controller.resource->isReady() && m_is_game_running)
		{
			initControllerRuntime(controller);
		}
	}


	Path getControllerSource(EntityRef entity) override
	{
		const auto& controller = m_controllers.get(entity);
		return controller.resource ? controller.resource->getPath() : Path("");
	}

	bool isPropertyAnimatorEnabled(EntityRef entity) override
	{
		return !m_property_animators.get(entity).flags.isSet(PropertyAnimator::DISABLED);
	}


	void enablePropertyAnimator(EntityRef entity, bool enabled) override
	{
		PropertyAnimator& animator = m_property_animators.get(entity);
		animator.flags.set(PropertyAnimator::DISABLED, !enabled);
		animator.time = 0;
		if (!enabled)
		{
			applyPropertyAnimator(entity, animator);
		}
	}


	Path getPropertyAnimation(EntityRef entity) override
	{
		const auto& animator = m_property_animators.get(entity);
		return animator.animation ? animator.animation->getPath() : Path("");
	}
	
	
	void setPropertyAnimation(EntityRef entity, const Path& path) override
	{
		auto& animator = m_property_animators.get(entity);
		animator.time = 0;
		unloadResource(animator.animation);
		animator.animation = loadPropertyAnimation(path);
	}


	Path getAnimation(EntityRef entity) override
	{
		const auto& animable = m_animables[entity];
		return animable.animation ? animable.animation->getPath() : Path("");
	}


	void setAnimation(EntityRef entity, const Path& path) override
	{
		auto& animable = m_animables[entity];
		unloadResource(animable.animation);
		animable.animation = loadAnimation(path);
		animable.time = 0;
	}


	void updateAnimable(Animable& animable, float time_delta) const
	{
		if (!animable.animation || !animable.animation->isReady()) return;
		EntityRef entity = animable.entity;
		if (!m_universe.hasComponent(entity, MODEL_INSTANCE_TYPE)) return;

		Model* model = m_render_scene->getModelInstanceModel(entity);
		if (!model->isReady()) return;

		Pose* pose = m_render_scene->lockPose(entity);
		if (!pose) return;

		model->getRelativePose(*pose);
		animable.animation->getRelativePose(animable.time, *pose, *model, nullptr);
		pose->computeAbsolute(*model);

		float t = animable.time + time_delta * animable.time_scale;
		float l = animable.animation->getLength();
		while (t > l) t -= l;
		animable.time = t;

		m_render_scene->unlockPose(entity, true);
	}


	void updateAnimable(EntityRef entity, float time_delta) override
	{
		Animable& animable = m_animables[entity];
		updateAnimable(animable, time_delta);
	}


	void updateController(EntityRef entity, float time_delta) override
	{
		Controller& controller = m_controllers.get(entity);
		updateController(controller, time_delta);
		processEventStream();
		m_event_stream.clear();
	}


	void setControllerInput(EntityRef entity, int input_idx, float value) override
	{
		Controller& ctrl = m_controllers.get(entity);
		Anim::InputDecl& decl = ctrl.resource->m_input_decl;
		if (!ctrl.root) return;
		if (input_idx >= lengthOf(decl.inputs)) return;
		if (decl.inputs[input_idx].type != Anim::InputDecl::FLOAT) return;
		*(float*)&ctrl.input[decl.inputs[input_idx].offset] = value;
	}


	void setControllerInput(EntityRef entity, int input_idx, bool value) override
	{
		Controller& ctrl = m_controllers.get(entity);
		Anim::InputDecl& decl = ctrl.resource->m_input_decl;
		if (!ctrl.root) return;
		if (input_idx >= lengthOf(decl.inputs)) return;
		if (decl.inputs[input_idx].type != Anim::InputDecl::BOOL) return;
		*(bool*)&ctrl.input[decl.inputs[input_idx].offset] = value;
	}


	void setControllerInput(EntityRef entity, int input_idx, int value) override
	{
		Controller& ctrl = m_controllers.get(entity);
		Anim::InputDecl& decl = ctrl.resource->m_input_decl;
		if (!ctrl.root) return;
		if (input_idx >= lengthOf(decl.inputs)) return;
		if (decl.inputs[input_idx].type != Anim::InputDecl::INT) return;
		*(int*)&ctrl.input[decl.inputs[input_idx].offset] = value;
	}


	Anim::ComponentInstance* getControllerRoot(EntityRef entity) override
	{
		return m_controllers.get(entity).root;
	}


	LocalRigidTransform getControllerRootMotion(EntityRef entity) override
	{
		Controller& ctrl = m_controllers.get(entity);
		return ctrl.root ? ctrl.root->getRootMotion() : LocalRigidTransform{{0, 0, 0}, {0, 0, 0, 1}};
	}

	
	u8* getControllerInput(EntityRef entity) override
	{
		auto& input = m_controllers.get(entity).input;
		return input.empty() ? nullptr : &input[0];
	}


	void applyControllerSet(EntityRef entity, const char* set_name) override
	{
		Controller& ctrl = m_controllers.get(entity);
		u32 set_name_hash = crc32(set_name);
		int set_idx = ctrl.resource->m_sets_names.find([set_name_hash](const StaticString<32>& val) {
			return crc32(val) == set_name_hash;
		});
		if (set_idx < 0) return;

		for (auto& entry : ctrl.resource->m_animation_set)
		{
			if (entry.set != set_idx) continue;
			ctrl.animations[entry.hash] = entry.animation;
		}
		if (ctrl.root) ctrl.root->onAnimationSetUpdated(ctrl.animations);
	}


	void setControllerDefaultSet(EntityRef entity, int set) override
	{
		Controller& ctrl = m_controllers.get(entity);
		ctrl.default_set = ctrl.resource ? crc32(ctrl.resource->m_sets_names[set]) : 0;
	}


	int getControllerDefaultSet(EntityRef entity) override
	{
		Controller& ctrl = m_controllers.get(entity);
		auto is_default_set = [&ctrl](const StaticString<32>& val) {
			return crc32(val) == ctrl.default_set;
		};
		int idx = 0;
		if(ctrl.resource) idx = ctrl.resource->m_sets_names.find(is_default_set);
		return idx < 0 ? 0 : idx;
	}


	Anim::ControllerResource* getControllerResource(EntityRef entity) override
	{
		return m_controllers.get(entity).resource;
	}


	bool initControllerRuntime(Controller& controller)
	{
		if (!controller.resource->isReady()) return false;
		if (controller.resource->m_input_decl.getSize() == 0) return false;
		controller.root = controller.resource->createInstance(m_allocator);
		controller.input.resize(controller.resource->m_input_decl.getSize());
		int set_idx = 0;
		for (int i = 0; i < controller.resource->m_sets_names.size(); ++i)
		{
			if (controller.default_set == crc32(controller.resource->m_sets_names[i]))
			{
				set_idx = i;
				break;
			}
		}
		for (auto& entry : controller.resource->m_animation_set)
		{
			if (entry.set != set_idx) continue;
			controller.animations.insert(entry.hash, entry.animation);
		}
		setMemory(&controller.input[0], 0, controller.input.size());
		Anim::RunningContext rc;
		rc.time_delta = 0;
		rc.allocator = &m_allocator;
		rc.input = &controller.input[0];
		rc.current = nullptr;
		rc.anim_set = &controller.animations;
		rc.event_stream = &m_event_stream;
		rc.controller = {controller.entity.index};
		controller.root->enter(rc, nullptr);
		return true;
	}


	void updateSharedController(SharedController& controller)
	{
		if (!controller.parent.isValid()) return;

		int parent_controller_idx = m_controllers.find((EntityRef)controller.parent);
		if (parent_controller_idx < 0) return;

		Controller& parent_controller = m_controllers.at(parent_controller_idx);
		if (!parent_controller.root) return;

		EntityRef entity = controller.entity;
		if (!m_universe.hasComponent(entity, MODEL_INSTANCE_TYPE)) return;

		Pose* pose = m_render_scene->lockPose(entity);
		if (!pose) return;

		Model* model = m_render_scene->getModelInstanceModel(entity);

		model->getPose(*pose);
		pose->computeRelative(*model);

		parent_controller.root->fillPose(m_engine, *pose, *model, 1, nullptr);

		pose->computeAbsolute(*model);
		m_render_scene->unlockPose(entity, true);
	}


	void updateController(Controller& controller, float time_delta)
	{
		if (!controller.resource->isReady())
		{
			LUMIX_DELETE(m_allocator, controller.root);
			controller.root = nullptr;
			return;
		}

		if (!controller.root && !initControllerRuntime(controller)) return;

		Anim::RunningContext rc;
		rc.time_delta = time_delta;
		rc.current = controller.root;
		rc.allocator = &m_allocator;
		rc.input = &controller.input[0];
		rc.anim_set = &controller.animations;
		rc.event_stream = &m_event_stream;
		rc.controller = {controller.entity.index};
		controller.root = controller.root->update(rc, true);

		EntityRef entity = controller.entity;
		if (!m_universe.hasComponent(entity, MODEL_INSTANCE_TYPE)) return;

		Model* model = m_render_scene->getModelInstanceModel(entity);
		if (!model || !model->isReady()) return;
			
		Pose* pose = m_render_scene->lockPose(entity);
		if (!pose) return;

		model->getRelativePose(*pose);

		controller.root->fillPose(m_engine, *pose, *model, 1, nullptr);

		for (Controller::IK& ik : controller.inverse_kinematics)
		{
			if (ik.weight == 0) break;

			updateIK(ik, *pose, *model);
		}

		pose->computeAbsolute(*model);
		m_render_scene->unlockPose(entity, true);
	}

	static LocalRigidTransform getAbsolutePosition(const Pose& pose, const Model& model, int bone_index)
	{
		LocalRigidTransform t{Vec3::ZERO, Quat::IDENTITY};
		const Model::Bone& bone = model.getBone(bone_index);
		LocalRigidTransform bone_transform{pose.positions[bone_index], pose.rotations[bone_index]};
		if (bone.parent_idx < 0)
		{
			return bone_transform;
		}
		return getAbsolutePosition(pose, model, bone.parent_idx) * bone_transform;
	}


	static void updateIK(Controller::IK& ik, Pose& pose, Model& model)
	{
		/*int indices[Controller::IK::MAX_BONES_COUNT];
		LocalRigidTransform transforms[Controller::IK::MAX_BONES_COUNT];
		Vec3 old_pos[Controller::IK::MAX_BONES_COUNT];
		float len[Controller::IK::MAX_BONES_COUNT - 1];
		float len_sum = 0;
		for (int i = 0; i < ik.bones_count; ++i)
		{
			auto iter = model.getBoneIndex(ik.bones[i]);
			if (!iter.isValid()) return;

			indices[i] = iter.value();
		}

		// convert from bone space to object space
		const Model::Bone& first_bone = model.getBone(indices[0]);
		LocalRigidTransform roots_parent;
		if (first_bone.parent_idx >= 0)
		{
			roots_parent = getAbsolutePosition(pose, model, first_bone.parent_idx);
		}
		else
		{
			roots_parent.pos = Vec3::ZERO;
			roots_parent.rot = Quat::IDENTITY;
		}

		LocalRigidTransform parent_tr = roots_parent;
		for (int i = 0; i < ik.bones_count; ++i)
		{
			LocalRigidTransform tr{pose.positions[indices[i]], pose.rotations[indices[i]]};
			transforms[i] = parent_tr * tr;
			old_pos[i] = transforms[i].pos;
			if (i > 0)
			{
				len[i - 1] = (transforms[i].pos - transforms[i - 1].pos).length();
				len_sum += len[i - 1];
			}
			parent_tr = transforms[i];
		}

		Vec3 target = ik.target;
		Vec3 to_target = target - transforms[0].pos;
		if (len_sum * len_sum < to_target.squaredLength()) {
			to_target.normalize();
			target = transforms[0].pos + to_target * len_sum;
		}

		for (int iteration = 0; iteration < ik.max_iterations; ++iteration)
		{
			transforms[ik.bones_count - 1].pos = target;
			
			for (int i = ik.bones_count - 1; i > 1; --i)
			{
				Vec3 dir = (transforms[i - 1].pos - transforms[i].pos).normalized();
				transforms[i - 1].pos = transforms[i].pos + dir * len[i - 1];
			}

			for (int i = 1; i < ik.bones_count; ++i)
			{
				Vec3 dir = (transforms[i].pos - transforms[i - 1].pos).normalized();
				transforms[i].pos = transforms[i - 1].pos + dir * len[i - 1];
			}
		}

		// compute rotations from new positions
		for (int i = ik.bones_count - 1; i >= 0; --i)
		{
			if (i < ik.bones_count - 1)
			{
				Vec3 old_d = old_pos[i + 1] - old_pos[i];
				Vec3 new_d = transforms[i + 1].pos - transforms[i].pos;
				old_d.normalize();
				new_d.normalize();

				Quat rel_rot = Quat::vec3ToVec3(old_d, new_d);
				transforms[i].rot = rel_rot * transforms[i].rot;
			}
		}

		// convert from object space to bone space
		for (int i = ik.bones_count - 1; i > 0; --i)
		{
			transforms[i] = transforms[i - 1].inverted() * transforms[i];
			pose.positions[indices[i]] = transforms[i].pos;
		}
		for (int i = ik.bones_count - 2; i > 0; --i)
		{
			pose.rotations[indices[i]] = transforms[i].rot;
		}

		if (first_bone.parent_idx >= 0)
		{
			pose.rotations[indices[0]] = roots_parent.rot.conjugated() * transforms[0].rot;
		}
		else
		{
			pose.rotations[indices[0]] = transforms[0].rot;
		}*/
		// TODO
		ASSERT(false);
	}


	void applyPropertyAnimator(EntityRef entity, PropertyAnimator& animator)
	{
		const PropertyAnimation* animation = animator.animation;
		int frame = int(animator.time * animation->fps + 0.5f);
		frame = frame % animation->curves[0].frames.back();
		for (PropertyAnimation::Curve& curve : animation->curves)
		{
			if (curve.frames.size() < 2) continue;
			for (int i = 1, n = curve.frames.size(); i < n; ++i)
			{
				if (frame <= curve.frames[i])
				{
					float t = (frame - curve.frames[i - 1]) / float(curve.frames[i] - curve.frames[i - 1]);
					float v = curve.values[i] * t + curve.values[i - 1] * (1 - t);
					ComponentUID cmp;
					cmp.type = curve.cmp_type;
					cmp.scene = m_universe.getScene(cmp.type);
					cmp.entity = entity;
					InputBlob blob(&v, sizeof(v));
					curve.property->setValue(cmp, -1, blob);
					break;
				}
			}
		}
	}


	void updatePropertyAnimators(float time_delta)
	{
		PROFILE_FUNCTION();
		for (int anim_idx = 0, c = m_property_animators.size(); anim_idx < c; ++anim_idx)
		{
			EntityRef entity = m_property_animators.getKey(anim_idx);
			PropertyAnimator& animator = m_property_animators.at(anim_idx);
			const PropertyAnimation* animation = animator.animation;
			if (!animation || !animation->isReady()) continue;
			if (animation->curves.empty()) continue;
			if (animation->curves[0].frames.empty()) continue;
			if (animator.flags.isSet(PropertyAnimator::DISABLED)) continue;

			animator.time += time_delta;
			
			applyPropertyAnimator(entity, animator);
		}
	}


	void updateAnimables(float time_delta)
	{
		PROFILE_FUNCTION();
		if (m_animables.size() == 0) return;

		JobSystem::JobDecl jobs[16];
		JobSystem::LambdaJob job_storage[16];

		int job_count = Math::minimum(lengthOf(jobs), m_animables.size());
		ASSERT(job_count > 0);
		volatile int counter = 0;
		for (int i = 0; i < job_count; ++i)
		{
			JobSystem::fromLambda([time_delta, this, i, job_count]() {
				PROFILE_BLOCK("Animate Job");
				int all_count = m_animables.size();
				int batch_count = all_count / job_count;
				if (i == job_count - 1) batch_count = all_count - (job_count - 1) * batch_count;
				for (int j = 0; j < batch_count; ++j)
				{
					Animable& animable = m_animables.at(j + i * all_count / job_count);
					AnimationSceneImpl::updateAnimable(animable, time_delta);
				}
			}, &job_storage[i], &jobs[i], nullptr);
		}
		JobSystem::runJobs(jobs, job_count, &counter);
		JobSystem::wait(&counter);
	}


	void update(float time_delta, bool paused) override
	{
		PROFILE_FUNCTION();
		if (!m_is_game_running) return;
		if (paused) return;

		m_event_stream.clear();

		updateAnimables(time_delta);
		updatePropertyAnimators(time_delta);

		for (Controller& controller : m_controllers)
		{
			AnimationSceneImpl::updateController(controller, time_delta);
		}

		for (SharedController& controller : m_shared_controllers)
		{
			updateSharedController(controller);
		}

		processEventStream();
	}


	void processEventStream()
	{
		InputBlob blob(m_event_stream);
		u32 set_input_type = crc32("set_input");
		while (blob.getPosition() < blob.getSize())
		{
			u32 type;
			u8 size;
			EntityRef entity;
			blob.read(type);
			blob.read(entity);
			blob.read(size);
			if (type == set_input_type)
			{
				Anim::SetInputEvent event;
				blob.read(event);
				Controller& ctrl = m_controllers.get(entity);
				if (ctrl.resource->isReady())
				{
					Anim::InputDecl& decl = ctrl.resource->m_input_decl;
					Anim::InputDecl::Input& input = decl.inputs[event.input_idx];
					switch (input.type)
					{
						case Anim::InputDecl::BOOL: *(bool*)&ctrl.input[input.offset] = event.b_value; break;
						case Anim::InputDecl::INT: *(int*)&ctrl.input[input.offset] = event.i_value; break;
						case Anim::InputDecl::FLOAT: *(float*)&ctrl.input[input.offset] = event.f_value; break;
						default: ASSERT(false); break;
					}
				}
			}
			else
			{
				blob.skip(size);
			}
		}
	}


	PropertyAnimation* loadPropertyAnimation(const Path& path) const
	{
		if (!path.isValid()) return nullptr;
		ResourceManagerHub& rm = m_engine.getResourceManager();
		return rm.load<PropertyAnimation>(path);
	}


	Animation* loadAnimation(const Path& path) const
	{
		ResourceManagerHub& rm = m_engine.getResourceManager();
		return rm.load<Animation>(path);
	}


	Anim::ControllerResource* loadController(const Path& path) const
	{
		ResourceManagerHub& rm = m_engine.getResourceManager();
		return rm.load<Anim::ControllerResource>(path);
	}


	void createPropertyAnimator(EntityRef entity)
	{
		PropertyAnimator& animator = m_property_animators.emplace(entity, m_allocator);
		animator.animation = nullptr;
		animator.time = 0;
		m_universe.onComponentCreated(entity, PROPERTY_ANIMATOR_TYPE, this);
	}


	void createAnimable(EntityRef entity)
	{
		Animable& animable = m_animables.insert(entity);
		animable.time = 0;
		animable.animation = nullptr;
		animable.entity = entity;
		animable.time_scale = 1;
		animable.start_time = 0;

		m_universe.onComponentCreated(entity, ANIMABLE_TYPE, this);
	}


	void createController(EntityRef entity)
	{
		Controller& controller = m_controllers.emplace(entity, m_allocator);
		controller.entity = entity;

		m_universe.onComponentCreated(entity, CONTROLLER_TYPE, this);
	}


	void createSharedController(EntityRef entity)
	{
		m_shared_controllers.insert(entity, {entity, INVALID_ENTITY});

		m_universe.onComponentCreated(entity, SHARED_CONTROLLER_TYPE, this);
	}


	IPlugin& getPlugin() const override { return m_anim_system; }


	IAllocator& m_allocator;
	Universe& m_universe;
	IPlugin& m_anim_system;
	Engine& m_engine;
	AssociativeArray<EntityRef, Animable> m_animables;
	AssociativeArray<EntityRef, PropertyAnimator> m_property_animators;
	AssociativeArray<EntityRef, Controller> m_controllers;
	AssociativeArray<EntityRef, SharedController> m_shared_controllers;
	RenderScene* m_render_scene;
	bool m_is_game_running;
	OutputBlob m_event_stream;
};


AnimationScene* AnimationScene::create(Engine& engine, IPlugin& plugin, Universe& universe, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, AnimationSceneImpl)(engine, plugin, universe, allocator);
}


void AnimationScene::destroy(AnimationScene& scene)
{
	AnimationSceneImpl& scene_impl = (AnimationSceneImpl&)scene;
	LUMIX_DELETE(scene_impl.m_allocator, &scene_impl);
}


void AnimationScene::registerLuaAPI(lua_State* L)
{
	#define REGISTER_FUNCTION(name) \
	do {\
		auto f = &LuaWrapper::wrapMethod<AnimationSceneImpl, decltype(&AnimationSceneImpl::name), &AnimationSceneImpl::name>; \
		LuaWrapper::createSystemFunction(L, "Animation", #name, f); \
	} while(false) \

	REGISTER_FUNCTION(getAnimationLength);
	REGISTER_FUNCTION(setControllerIntInput);
	REGISTER_FUNCTION(setControllerBoolInput);
	REGISTER_FUNCTION(setControllerFloatInput);
	REGISTER_FUNCTION(getControllerInputIndex);

	#undef REGISTER_FUNCTION

	LuaWrapper::createSystemFunction(L, "Animation", "setIK", &AnimationSceneImpl::setIK); \

}


}
