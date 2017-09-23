#include "animation_system.h"

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



struct AnimationSceneImpl LUMIX_FINAL : public AnimationScene
{
	friend struct AnimationSystemImpl;

	struct SharedController
	{
		Entity entity;
		Entity parent;
	};

	struct Controller
	{
		Controller(IAllocator& allocator) : input(allocator), animations(allocator) {}

		Entity entity;
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
			Vec3 target;
		} inverse_kinematics[4];
	};


	struct Animable
	{
		float time;
		float time_scale;
		float start_time;
		Animation* animation;
		Entity entity;
	};


	AnimationSceneImpl(AnimationSystemImpl& anim_system, Engine& engine, Universe& universe, IAllocator& allocator)
		: m_universe(universe)
		, m_engine(engine)
		, m_anim_system(anim_system)
		, m_animables(allocator)
		, m_controllers(allocator)
		, m_shared_controllers(allocator)
		, m_event_stream(allocator)
	{
		m_is_game_running = false;
		m_render_scene = static_cast<RenderScene*>(universe.getScene(crc32("renderer")));
		universe.registerComponentType(ANIMABLE_TYPE, this, &AnimationSceneImpl::serializeAnimable, &AnimationSceneImpl::deserializeAnimable);
		universe.registerComponentType(CONTROLLER_TYPE, this, &AnimationSceneImpl::serializeController, &AnimationSceneImpl::deserializeController);
		universe.registerComponentType(SHARED_CONTROLLER_TYPE, this, &AnimationSceneImpl::serializeSharedController, &AnimationSceneImpl::deserializeSharedController);
		ASSERT(m_render_scene);
	}


	void serializeSharedController(ISerializer& serializer, ComponentHandle cmp)
	{
		SharedController& ctrl = m_shared_controllers[{cmp.index}];
		serializer.write("parent", ctrl.parent);
	}


	void deserializeSharedController(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		Entity parent;
		serializer.read(&parent);
		m_shared_controllers.insert(entity, {entity, parent});
		m_universe.addComponent(entity, SHARED_CONTROLLER_TYPE, this, {entity.index});
	}


	void serializeAnimable(ISerializer& serializer, ComponentHandle cmp)
	{
		Animable& animable = m_animables[{cmp.index}];
		serializer.write("time_scale", animable.time_scale);
		serializer.write("start_time", animable.start_time);
		serializer.write("animation", animable.animation ? animable.animation->getPath().c_str() : "");
	}


	void deserializeAnimable(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		Animable& animable = m_animables.insert(entity);
		animable.entity = entity;
		serializer.read(&animable.time_scale);
		serializer.read(&animable.start_time);
		char tmp[MAX_PATH_LENGTH];
		serializer.read(tmp, lengthOf(tmp));
		auto* res = tmp[0] ? m_engine.getResourceManager().get(ANIMATION_TYPE)->load(Path(tmp)) : nullptr;
		animable.animation = (Animation*)res;
		m_universe.addComponent(entity, ANIMABLE_TYPE, this, {entity.index});
	}


	void serializeController(ISerializer& serializer, ComponentHandle cmp)
	{
		Controller& controller = m_controllers.get({cmp.index});
		serializer.write("source", controller.resource ? controller.resource->getPath().c_str() : "");
		serializer.write("default_set", controller.default_set);
	}


	int getVersion() const override { return (int)AnimationSceneVersion::LATEST; }


	void deserializeController(IDeserializer& serializer, Entity entity, int scene_version)
	{
		Controller& controller = m_controllers.emplace(entity, m_anim_system.m_allocator);
		controller.entity = entity;
		char tmp[MAX_PATH_LENGTH];
		serializer.read(tmp, lengthOf(tmp));
		if (scene_version > (int)AnimationSceneVersion::SHARED_CONTROLLER)
		{
			serializer.read(&controller.default_set);
		}
		auto* res = tmp[0] ? m_engine.getResourceManager().get(CONTROLLER_RESOURCE_TYPE)->load(Path(tmp)) : nullptr;
		setControllerResource(controller, (Anim::ControllerResource*)res);
		m_universe.addComponent(entity, CONTROLLER_TYPE, this, {entity.index});
	}


	const OutputBlob& getEventStream() const override
	{
		return m_event_stream;
	}


	void clear() override
	{
		for (Animable& animable : m_animables)
		{
			unloadAnimation(animable.animation);
		}
		m_animables.clear();

		for (Controller& controller : m_controllers)
		{
			unloadController(controller.resource);
			setControllerResource(controller, nullptr);
		}
		m_controllers.clear();
	}


	static int setIK(lua_State* L)
	{
		AnimationSceneImpl* scene = LuaWrapper::checkArg<AnimationSceneImpl*>(L, 1);
		ComponentHandle cmp = LuaWrapper::checkArg<ComponentHandle>(L, 2);
		Controller& controller = scene->m_controllers.get({ cmp.index });
		int index = LuaWrapper::checkArg<int>(L, 3);
		Controller::IK& ik = controller.inverse_kinematics[index];
		ik.weight = LuaWrapper::checkArg<float>(L, 4);
		ik.target = LuaWrapper::checkArg<Vec3>(L, 5);
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
		}
		return 0;
	}


	int getControllerInputIndex(ComponentHandle cmp, const char* name) const
	{
		const Controller& controller = m_controllers[{cmp.index}];
		Anim::InputDecl& decl = controller.resource->m_input_decl;
		for (int i = 0; i < lengthOf(decl.inputs); ++i)
		{
			if (decl.inputs[i].type != Anim::InputDecl::EMPTY && equalStrings(decl.inputs[i].name, name)) return i;
		}
		return -1;
	}


	void setControllerFloatInput(ComponentHandle cmp, int input_idx, float value)
	{
		Controller& controller = m_controllers.get({ cmp.index });
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


	void setControllerIntInput(ComponentHandle cmp, int input_idx, int value)
	{
		Controller& controller = m_controllers.get({ cmp.index });
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


	void setControllerBoolInput(ComponentHandle cmp, int input_idx, bool value)
	{
		Controller& controller = m_controllers.get({ cmp.index });
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


	float getAnimationLength(int animation_idx)
	{
		auto* animation = static_cast<Animation*>(animation_idx > 0 ? m_engine.getLuaResource(animation_idx) : nullptr);
		if (animation) return animation->getLength();
		return 0;
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
			LUMIX_DELETE(m_anim_system.m_allocator, controller.root);
			controller.root = nullptr;
		}
		m_is_game_running = false;
	}
	
	
	Universe& getUniverse() override { return m_universe; }


	ComponentHandle getComponent(Entity entity, ComponentType type) override
	{
		if (type == ANIMABLE_TYPE)
		{
			if (m_animables.find(entity) < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		else if (type == CONTROLLER_TYPE)
		{
			if (m_controllers.find(entity) < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		else if (type == SHARED_CONTROLLER_TYPE)
		{
			if (m_shared_controllers.find(entity) < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		return INVALID_COMPONENT;
	}


	ComponentHandle createComponent(ComponentType type, Entity entity) override
	{
		if (type == ANIMABLE_TYPE) return createAnimable(entity);
		if (type == CONTROLLER_TYPE) return createController(entity);
		if (type == SHARED_CONTROLLER_TYPE) return createSharedController(entity);
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
			if ((controller.resource == &resource) && (controller.root != nullptr) && (new_state != Resource::State::READY))
			{
				LUMIX_DELETE(m_engine.getAllocator(), controller.root);
				controller.root = nullptr;
				controller.default_set = 0;
				controller.animations.clear();
				controller.input.clear();
			}
		}
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
		else if (type == CONTROLLER_TYPE)
		{
			Entity entity = {component.index};
			auto& controller = m_controllers.get(entity);
			unloadController(controller.resource);
			setControllerResource(controller, nullptr);
			m_controllers.erase(entity);
			m_universe.destroyComponent(entity, type, this, component);
		}
		else if (type == SHARED_CONTROLLER_TYPE)
		{
			Entity entity = {component.index};
			m_shared_controllers.erase(entity);
			m_universe.destroyComponent(entity, type, this, component);
		}
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
			ComponentHandle cmp = {animable.entity.index};
			m_universe.addComponent(animable.entity, ANIMABLE_TYPE, this, cmp);
		}

		serializer.read(count);
		m_controllers.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			Controller controller(m_anim_system.m_allocator);
			serializer.read(controller.default_set);
			serializer.read(controller.entity);
			char tmp[MAX_PATH_LENGTH];
			serializer.readString(tmp, lengthOf(tmp));
			setControllerResource(controller, tmp[0] ? loadController(Path(tmp)) : nullptr);
			m_controllers.insert(controller.entity, controller);
			ComponentHandle cmp = { controller.entity.index };
			m_universe.addComponent(controller.entity, CONTROLLER_TYPE, this, cmp);
		}

		serializer.read(count);
		m_shared_controllers.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			SharedController controller;
			serializer.read(controller.entity);
			serializer.read(controller.parent);
			m_shared_controllers.insert(controller.entity, controller);
			ComponentHandle cmp = {controller.entity.index};
			m_universe.addComponent(controller.entity, SHARED_CONTROLLER_TYPE, this, cmp);
		}
	}


	void setSharedControllerParent(ComponentHandle cmp, Entity parent) override
	{
		m_shared_controllers[{cmp.index}].parent = parent;
	}


	Entity getSharedControllerParent(ComponentHandle cmp) override { return m_shared_controllers[{cmp.index}].parent; }


	float getAnimableTimeScale(ComponentHandle cmp) { return m_animables[{cmp.index}].time_scale; }
	void setAnimableTimeScale(ComponentHandle cmp, float time_scale) { m_animables[{cmp.index}].time_scale = time_scale; }
	float getAnimableStartTime(ComponentHandle cmp) { return m_animables[{cmp.index}].start_time; }
	void setAnimableStartTime(ComponentHandle cmp, float time) { m_animables[{cmp.index}].start_time = time; }


	void setControllerSource(ComponentHandle cmp, const Path& path) override
	{
		auto& controller = m_controllers.get({cmp.index});
		unloadController(controller.resource);
		setControllerResource(controller, loadController(path));
		if (controller.resource->isReady() && m_is_game_running)
		{
			initControllerRuntime(controller);
		}
	}


	Path getControllerSource(ComponentHandle cmp) override
	{
		const auto& controller = m_controllers.get({cmp.index});
		return controller.resource ? controller.resource->getPath() : Path("");
	}


	Path getAnimation(ComponentHandle cmp) override
	{
		const auto& animable = m_animables[{cmp.index}];
		return animable.animation ? animable.animation->getPath() : Path("");
	}


	void setAnimation(ComponentHandle cmp, const Path& path) override
	{
		auto& animable = m_animables[{cmp.index}];
		unloadAnimation(animable.animation);
		animable.animation = loadAnimation(path);
		animable.time = 0;
	}


	void updateAnimable(Animable& animable, float time_delta) const
	{
		if (!animable.animation || !animable.animation->isReady()) return;
		ComponentHandle model_instance = m_render_scene->getModelInstanceComponent(animable.entity);
		if (model_instance == INVALID_COMPONENT) return;

		Model* model = m_render_scene->getModelInstanceModel(model_instance);
		if (!model->isReady()) return;

		Pose* pose = m_render_scene->lockPose(model_instance);
		if (!pose) return;

		model->getRelativePose(*pose);
		animable.animation->getRelativePose(animable.time, *pose, *model);
		pose->computeAbsolute(*model);

		float t = animable.time + time_delta * animable.time_scale;
		float l = animable.animation->getLength();
		while (t > l) t -= l;
		animable.time = t;

		m_render_scene->unlockPose(model_instance, true);
	}


	void updateAnimable(ComponentHandle cmp, float time_delta) override
	{
		Animable& animable = m_animables[{cmp.index}];
		updateAnimable(animable, time_delta);
	}


	void updateController(ComponentHandle cmp, float time_delta) override
	{
		Controller& controller = m_controllers.get({cmp.index});
		updateController(controller, time_delta);
		processEventStream();
		m_event_stream.clear();
	}


	void setControllerInput(ComponentHandle cmp, int input_idx, float value) override
	{
		Controller& ctrl = m_controllers.get({ cmp.index });
		Anim::InputDecl& decl = ctrl.resource->m_input_decl;
		if (!ctrl.root) return;
		if (input_idx >= lengthOf(decl.inputs)) return;
		if (decl.inputs[input_idx].type != Anim::InputDecl::FLOAT) return;
		*(float*)&ctrl.input[decl.inputs[input_idx].offset] = value;
	}


	void setControllerInput(ComponentHandle cmp, int input_idx, bool value) override
	{
		Controller& ctrl = m_controllers.get({ cmp.index });
		Anim::InputDecl& decl = ctrl.resource->m_input_decl;
		if (!ctrl.root) return;
		if (input_idx >= lengthOf(decl.inputs)) return;
		if (decl.inputs[input_idx].type != Anim::InputDecl::BOOL) return;
		*(bool*)&ctrl.input[decl.inputs[input_idx].offset] = value;
	}


	void setControllerInput(ComponentHandle cmp, int input_idx, int value) override
	{
		Controller& ctrl = m_controllers.get({ cmp.index });
		Anim::InputDecl& decl = ctrl.resource->m_input_decl;
		if (!ctrl.root) return;
		if (input_idx >= lengthOf(decl.inputs)) return;
		if (decl.inputs[input_idx].type != Anim::InputDecl::INT) return;
		*(bool*)&ctrl.input[decl.inputs[input_idx].offset] = value;
	}


	Anim::ComponentInstance* getControllerRoot(ComponentHandle cmp) override
	{
		return m_controllers.get({cmp.index}).root;
	}


	RigidTransform getControllerRootMotion(ComponentHandle cmp) override
	{
		Controller& ctrl = m_controllers.get({cmp.index});
		return ctrl.root ? ctrl.root->getRootMotion() : RigidTransform({0, 0, 0}, {0, 0, 0, 1});
	}


	Entity getControllerEntity(ComponentHandle cmp) override { return {cmp.index}; }


	u8* getControllerInput(ComponentHandle cmp)
	{
		auto& input = m_controllers.get({cmp.index}).input;
		return input.empty() ? nullptr : &input[0];
	}


	void applyControllerSet(ComponentHandle cmp, const char* set_name) override
	{
		Controller& ctrl = m_controllers.get({cmp.index});
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


	void setControllerDefaultSet(ComponentHandle cmp, int set) override
	{
		Controller& ctrl = m_controllers.get({cmp.index});
		ctrl.default_set = ctrl.resource ? crc32(ctrl.resource->m_sets_names[set]) : 0;
	}


	int getControllerDefaultSet(ComponentHandle cmp) override
	{
		Controller& ctrl = m_controllers.get({ cmp.index });
		auto is_default_set = [&ctrl](const StaticString<32>& val) {
			return crc32(val) == ctrl.default_set;
		};
		int idx = 0;
		if(ctrl.resource) idx = ctrl.resource->m_sets_names.find(is_default_set);
		return idx < 0 ? 0 : idx;
	}


	Anim::ControllerResource* getControllerResource(ComponentHandle cmp) override
	{
		return m_controllers.get({cmp.index}).resource;
	}


	bool initControllerRuntime(Controller& controller)
	{
		if (!controller.resource->isReady()) return false;
		if (controller.resource->m_input_decl.getSize() == 0) return false;
		controller.root = controller.resource->createInstance(m_anim_system.m_allocator);
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
		rc.allocator = &m_anim_system.m_allocator;
		rc.input = &controller.input[0];
		rc.current = nullptr;
		rc.anim_set = &controller.animations;
		rc.event_stream = &m_event_stream;
		rc.controller = {controller.entity.index};
		controller.root->enter(rc, nullptr);
		return true;
	}


	void updateSharedController(SharedController& controller, float time_delta)
	{
		if (!controller.parent.isValid()) return;

		int parent_controller_idx = m_controllers.find(controller.parent);
		if (parent_controller_idx < 0) return;

		Controller& parent_controller = m_controllers.at(parent_controller_idx);
		if (!parent_controller.root) return;

		ComponentHandle model_instance = m_render_scene->getModelInstanceComponent(controller.entity);
		if (model_instance == INVALID_COMPONENT) return;

		Pose* pose = m_render_scene->lockPose(model_instance);
		if (!pose) return;

		Model* model = m_render_scene->getModelInstanceModel(model_instance);

		model->getPose(*pose);
		pose->computeRelative(*model);

		parent_controller.root->fillPose(m_anim_system.m_engine, *pose, *model, 1);

		pose->computeAbsolute(*model);
		m_render_scene->unlockPose(model_instance, true);
	}


	void updateController(Controller& controller, float time_delta)
	{
		if (!controller.resource->isReady())
		{
			LUMIX_DELETE(m_anim_system.m_allocator, controller.root);
			controller.root = nullptr;
			return;
		}

		if (!controller.root && !initControllerRuntime(controller)) return;

		Anim::RunningContext rc;
		rc.time_delta = time_delta;
		rc.current = controller.root;
		rc.allocator = &m_anim_system.m_allocator;
		rc.input = &controller.input[0];
		rc.anim_set = &controller.animations;
		rc.event_stream = &m_event_stream;
		rc.controller = {controller.entity.index};
		controller.root = controller.root->update(rc, true);

		ComponentHandle model_instance = m_render_scene->getModelInstanceComponent(controller.entity);
		if (model_instance == INVALID_COMPONENT) return;

		Pose* pose = m_render_scene->lockPose(model_instance);
		if (!pose) return;

		Model* model = m_render_scene->getModelInstanceModel(model_instance);

		model->getPose(*pose);
		pose->computeRelative(*model);

		controller.root->fillPose(m_anim_system.m_engine, *pose, *model, 1);

		pose->computeAbsolute(*model);

		for (Controller::IK& ik : controller.inverse_kinematics)
		{
			if (ik.weight == 0) break;

			updateIK(ik, *pose, *model, controller.entity);
		}
		m_render_scene->unlockPose(model_instance, true);
	}


	void updateIK(Controller::IK& ik, Pose& pose, Model& model, Entity& entity)
	{
		decltype(model.getBoneIndex(0)) bones_iters[Controller::IK::MAX_BONES_COUNT];
		for (int i = 0; i < ik.bones_count; ++i)
		{
			bones_iters[i] = model.getBoneIndex(ik.bones[i]);
			if (!bones_iters[i].isValid()) return;
		}
		
		int indices[Controller::IK::MAX_BONES_COUNT];
		Vec3 pos[Controller::IK::MAX_BONES_COUNT];
		float len[Controller::IK::MAX_BONES_COUNT - 1];
		float len_sum = 0;
		for (int i = 0; i < ik.bones_count; ++i)
		{
			indices[i] = bones_iters[i].value();
			pos[i] = pose.positions[indices[i]];
			if (i > 0)
			{
				len[i - 1] = (pos[i] - pos[i - 1]).length();
				len_sum += len[i - 1];
			}
		}

		Vec3 target = ik.target;
		Vec3 to_target = target - pos[0];
		if (len_sum * len_sum < to_target.squaredLength()) {
			to_target.normalize();
			target = pos[0] + to_target * len_sum;
		}

		for (int iteration = 0; iteration < ik.max_iterations; ++iteration)
		{
			pos[ik.bones_count - 1] = target;
			
			// backward
			for (int i = ik.bones_count - 1; i > 0; --i)
			{
				Vec3 dir = (pos[i - 1] - pos[i]).normalized();
				pos[i - 1] = pos[i] + dir * len[i - 1];
			}

			// backward
			for (int i = 1; i < ik.bones_count; ++i)
			{
				Vec3 dir = (pos[i] - pos[i - 1]).normalized();
				pos[i] = pos[i - 1] + dir * len[i - 1];
			}
		}

		for (int i = 0; i < ik.bones_count; ++i)
		{
			if (i < ik.bones_count - 1)
			{
				Vec3 old_d = pose.positions[indices[i + 1]] - pose.positions[indices[i]];
				Vec3 new_d = pos[i + 1] - pos[i];
				old_d.normalize();
				new_d.normalize();

				Quat rel_rot = Quat::vec3ToVec3(old_d, new_d);
				pose.rotations[indices[i]] = rel_rot * pose.rotations[indices[i]];
			}
			pose.positions[indices[i]] = pos[i];
		}
	}


	void updateAnimables(float time_delta)
	{
		if (m_animables.size() == 0) return;

		IAllocator& allocator = m_engine.getAllocator();
		JobSystem::LambdaJob jobs[16];

		int job_count = Math::minimum(lengthOf(jobs), m_animables.size());
		ASSERT(job_count > 0);
		volatile int counter = 0;
		for (int i = 0; i < job_count; ++i)
		{
			JobSystem::fromLambda([time_delta, this, i, job_count]() {
				PROFILE_BLOCK("Animate Job");
				int all_count = m_animables.size();
				int batch_count = all_count / job_count;
				if (i == job_count - 1) batch_count = all_count - ((job_count - 1) * batch_count);
				for (int j = 0; j < batch_count; ++j)
				{
					Animable& animable = m_animables.at(j + i * all_count / job_count);
					AnimationSceneImpl::updateAnimable(animable, time_delta);
				}
			}, &jobs[i], nullptr);
		}
		JobSystem::runJobs(jobs, job_count, &counter);
		JobSystem::waitOutsideJob(&counter);
	}


	void update(float time_delta, bool paused) override
	{
		PROFILE_FUNCTION();
		if (!m_is_game_running) return;
		if (paused) return;

		m_event_stream.clear();

		updateAnimables(time_delta);

		for (Controller& controller : m_controllers)
		{
			AnimationSceneImpl::updateController(controller, time_delta);
		}

		for (SharedController& controller : m_shared_controllers)
		{
			AnimationSceneImpl::updateSharedController(controller, time_delta);
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
			ComponentHandle cmp;
			blob.read(type);
			blob.read(cmp);
			blob.read(size);
			if (type == set_input_type)
			{
				Anim::SetInputEvent event;
				blob.read(event);
				Controller& ctrl = m_controllers.get({cmp.index});
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
		Controller& controller = m_controllers.emplace(entity, m_anim_system.m_allocator);
		controller.entity = entity;
		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, CONTROLLER_TYPE, this, cmp);
		return cmp;
	}


	ComponentHandle createSharedController(Entity entity)
	{
		m_shared_controllers.insert(entity, {entity, INVALID_ENTITY});
		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, SHARED_CONTROLLER_TYPE, this, cmp);
		return cmp;
	}


	IPlugin& getPlugin() const override { return m_anim_system; }


	Universe& m_universe;
	AnimationSystemImpl& m_anim_system;
	Engine& m_engine;
	AssociativeArray<Entity, Animable> m_animables;
	AssociativeArray<Entity, Controller> m_controllers;
	AssociativeArray<Entity, SharedController> m_shared_controllers;
	RenderScene* m_render_scene;
	bool m_is_game_running;
	OutputBlob m_event_stream;
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
		LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationSceneImpl>)("Source",
			&AnimationSceneImpl::getControllerSource,
			&AnimationSceneImpl::setControllerSource,
			"Animation controller (*.act)",
			CONTROLLER_RESOURCE_TYPE));
	PropertyRegister::add("anim_controller", LUMIX_NEW(m_allocator, AnimSetPropertyDescriptor)("Default set"));

	PropertyRegister::add("animable",
		LUMIX_NEW(m_allocator, ResourcePropertyDescriptor<AnimationSceneImpl>)("Animation",
			&AnimationSceneImpl::getAnimation,
			&AnimationSceneImpl::setAnimation,
			"Animation (*.ani)",
			ANIMATION_TYPE));
	PropertyRegister::add("animable",
		LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationSceneImpl>)(
			"Start time", &AnimationSceneImpl::getAnimableStartTime, &AnimationSceneImpl::setAnimableStartTime, 0, FLT_MAX, 0.1f));
	PropertyRegister::add("animable",
		LUMIX_NEW(m_allocator, DecimalPropertyDescriptor<AnimationSceneImpl>)(
			"Time scale", &AnimationSceneImpl::getAnimableTimeScale, &AnimationSceneImpl::setAnimableTimeScale, 0, FLT_MAX, 0.1f));

	PropertyRegister::add("shared_anim_controller",
		LUMIX_NEW(m_allocator, EntityPropertyDescriptor<AnimationSceneImpl>)(
			"Parent", &AnimationSceneImpl::getSharedControllerParent, &AnimationSceneImpl::setSharedControllerParent));


	registerLuaAPI();
}


AnimationSystemImpl::~AnimationSystemImpl()
{
	m_animation_manager.destroy();
	m_controller_manager.destroy();
}


void AnimationSystemImpl::registerLuaAPI()
{
	lua_State* L = m_engine.getState();
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


void AnimationSystemImpl::createScenes(Universe& ctx)
{
	auto* scene = LUMIX_NEW(m_allocator, AnimationSceneImpl)(*this, m_engine, ctx, m_allocator);
	ctx.addScene(scene);
}


void AnimationSystemImpl::destroyScene(IScene* scene) { LUMIX_DELETE(m_allocator, scene); }


LUMIX_PLUGIN_ENTRY(animation)
{
	return LUMIX_NEW(engine.getAllocator(), AnimationSystemImpl)(engine);
}
}
