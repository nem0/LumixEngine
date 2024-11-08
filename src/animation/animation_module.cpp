#include "animation_module.h"

#include "core/associative_array.h"
#include "core/atomic.h"
#include "core/hash.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/stream.h"

#include "animation/animation.h"
#include "animation/controller.h"
#include "animation/property_animation.h"
#include "engine/engine.h"
#include "engine/engine_hash_funcs.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "nodes.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_module.h"


namespace Lumix
{

struct Animation;
struct Engine;
struct World;

enum class AnimationModuleVersion {
	USE_ROOT_MOTION,

	LATEST
};


static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType ANIMABLE_TYPE = reflection::getComponentType("animable");
static const ComponentType PROPERTY_ANIMATOR_TYPE = reflection::getComponentType("property_animator");
static const ComponentType ANIMATOR_TYPE = reflection::getComponentType("animator");


struct AnimationModuleImpl final : AnimationModule {
	friend struct AnimationSystemImpl;
	
	struct Animator {
		enum Flags : u32 {
			NONE = 0,
			USE_ROOT_MOTION = 1 << 0
		};

		EntityRef entity;
		anim::Controller* resource = nullptr;
		u32 default_set = 0;
		Flags flags = Flags::NONE;
		anim::RuntimeContext* ctx = nullptr;
		LocalRigidTransform root_motion = {{0, 0, 0}, {0, 0, 0, 1}};
	};


	struct PropertyAnimator {
		struct Key {
			int frame0;
			int frame1;
			float value0;
			float value1;
		};

		enum Flags {
			NONE = 0,
			LOOPED = 1 << 0,
			DISABLED = 1 << 1
		};

		PropertyAnimator(IAllocator& allocator) : keys(allocator) {}

		PropertyAnimation* animation;
		Array<Key> keys;

		Flags flags = Flags::NONE;
		float time;
	};


	AnimationModuleImpl(Engine& engine, ISystem& anim_system, World& world, IAllocator& allocator)
		: m_world(world)
		, m_engine(engine)
		, m_anim_system(anim_system)
		, m_animables(allocator)
		, m_property_animators(allocator)
		, m_animators(allocator)
		, m_allocator(allocator)
		, m_animator_map(allocator)
	{
		m_is_game_running = false;
	}

	void init() override {
		m_render_module = static_cast<RenderModule*>(m_world.getModule("renderer"));
		ASSERT(m_render_module);
	}


	int getVersion() const override { return (int)AnimationModuleVersion::LATEST; }

	const char* getName() const override { return "animation"; }

	~AnimationModuleImpl() {
		for (PropertyAnimator& anim : m_property_animators) {
			unloadResource(anim.animation);
		}

		for (Animable& animable : m_animables) {
			unloadResource(animable.animation);
		}

		for (Animator& animator : m_animators) {
			unloadResource(animator.resource);
			setSource(animator, nullptr);
		}
	}

	i32 getAnimatorInputIndex(EntityRef entity, const char* name) const override {
		const Animator& animator = m_animators[m_animator_map[entity]];
		for (anim::Controller::Input& input : animator.resource->m_inputs) {
			if (input.name ==  name) return i32(&input - animator.resource->m_inputs.begin());
		}
		return -1;
	}


	void setAnimatorFloatInput(EntityRef entity, u32 input_idx, float value) {
		auto iter = m_animator_map.find(entity);
		if (!iter.isValid()) return;

		Animator& animator = m_animators[iter.value()];
		if (animator.resource->m_inputs[input_idx].type == anim::Value::NUMBER) {
			animator.ctx->inputs[input_idx].f = value;
		}
		else {
			logWarning("Trying to set float to ", animator.resource->m_inputs[input_idx].name);
		}
	}


	void setAnimatorBoolInput(EntityRef entity, u32 input_idx, bool value) {
		auto iter = m_animator_map.find(entity);
		if (!iter.isValid()) return;

		Animator& animator = m_animators[iter.value()];
		if (animator.resource->m_inputs[input_idx].type == anim::Value::BOOL) {
			animator.ctx->inputs[input_idx].b = value;
		}
		else {
			logWarning("Trying to set bool to ", animator.resource->m_inputs[input_idx].name);
		}
	}


	Animable& getAnimable(EntityRef entity) override
	{
		return m_animables[entity];
	}


	Animation* getAnimableAnimation(EntityRef entity) override
	{
		return m_animables[entity].animation;
	}

	
	void startGame() override 
	{
		m_is_game_running = true;
	}
	
	
	void stopGame() override
	{
		m_is_game_running = false;
	}
	
	
	World& getWorld() override { return m_world; }


	static void unloadResource(Resource* res)
	{
		if (!res) return;

		res->decRefCount();
	}


	void setSource(Animator& animator, anim::Controller* res)
	{
		if (animator.resource == res) return;
		if (animator.resource != nullptr) {
			if (animator.ctx) {
				animator.resource->destroyRuntime(*animator.ctx);
				animator.ctx = nullptr;
			}
			animator.resource->getObserverCb().unbind<&AnimationModuleImpl::onControllerResourceChanged>(this);
		}
		animator.resource = res;
		if (animator.resource != nullptr) {
			animator.resource->onLoaded<&AnimationModuleImpl::onControllerResourceChanged>(this);
		}
	}


	void onControllerResourceChanged(Resource::State old_state, Resource::State new_state, Resource& resource)
	{
		for (Animator& animator : m_animators) {
			if (animator.resource == &resource) {
				if(new_state == Resource::State::READY) {
					if (!animator.ctx) {
						animator.ctx = animator.resource->createRuntime(animator.default_set);
					}
				}
				else {
					if (animator.ctx) {
						animator.resource->destroyRuntime(*animator.ctx);
						animator.ctx = nullptr;
					}
				}
			}
		}
	}


	void destroyPropertyAnimator(EntityRef entity)
	{
		int idx = m_property_animators.find(entity);
		auto& animator = m_property_animators.at(idx);
		unloadResource(animator.animation);
		m_property_animators.erase(entity);
		m_world.onComponentDestroyed(entity, PROPERTY_ANIMATOR_TYPE, this);
	}


	void destroyAnimable(EntityRef entity)
	{
		auto& animable = m_animables[entity];
		unloadResource(animable.animation);
		m_animables.erase(entity);
		m_world.onComponentDestroyed(entity, ANIMABLE_TYPE, this);
	}


	void destroyAnimator(EntityRef entity)
	{
		const u32 idx = m_animator_map[entity];
		Animator& animator = m_animators[idx];
		unloadResource(animator.resource);
		setSource(animator, nullptr);
		const Animator& last = m_animators.back();
		m_animator_map[last.entity] = idx;
		m_animator_map.erase(entity);
		m_animators.swapAndPop(idx);
		m_world.onComponentDestroyed(entity, ANIMATOR_TYPE, this);
	}


	void serialize(OutputMemoryStream& serializer) override
	{
		serializer.write((u32)m_animables.size());
		for (const Animable& animable : m_animables)
		{
			serializer.write(animable.entity);
			serializer.writeString(animable.animation ? animable.animation->getPath() : Path());
		}

		serializer.write((u32)m_property_animators.size());
		for (int i = 0, n = m_property_animators.size(); i < n; ++i)
		{
			const PropertyAnimator& animator = m_property_animators.at(i);
			EntityRef entity = m_property_animators.getKey(i);
			serializer.write(entity);
			serializer.writeString(animator.animation ? animator.animation->getPath() : Path());
			serializer.write(animator.flags);
		}

		serializer.write((u32)m_animators.size());
		for (const Animator& animator : m_animators)
		{
			serializer.write(animator.default_set);
			serializer.write(animator.entity);
			serializer.write(animator.flags);
			serializer.writeString(animator.resource ? animator.resource->getPath() : Path());
		}
	}


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
	{
		u32 count;
		serializer.read(count);
		m_animables.reserve(count + m_animables.size());
		for (u32 i = 0; i < count; ++i)
		{
			Animable animable;
			serializer.read(animable.entity);
			animable.entity = entity_map.get(animable.entity);
			animable.time = Time::fromSeconds(0);

			const char* path = serializer.readString();
			animable.animation = path[0] == '\0' ? nullptr : loadAnimation(Path(path));
			m_animables.insert(animable.entity, animable);
			m_world.onComponentCreated(animable.entity, ANIMABLE_TYPE, this);
		}

		serializer.read(count);
		m_property_animators.reserve(count + m_property_animators.size());
		for (u32 i = 0; i < count; ++i)
		{
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);

			PropertyAnimator& animator = m_property_animators.emplace(entity, m_allocator);
			const char* path = serializer.readString();
			serializer.read(animator.flags);
			animator.time = 0;
			animator.animation = loadPropertyAnimation(Path(path));
			m_world.onComponentCreated(entity, PROPERTY_ANIMATOR_TYPE, this);
		}


		serializer.read(count);
		m_animators.reserve(m_animators.size() + count);
		for (u32 i = 0; i < count; ++i)
		{
			Animator animator;
			serializer.read(animator.default_set);
			serializer.read(animator.entity);
			if (version > (i32)AnimationModuleVersion::USE_ROOT_MOTION) {
				serializer.read(animator.flags);
			}
			animator.entity = entity_map.get(animator.entity);

			const char* tmp = serializer.readString();
			setSource(animator, tmp[0] ? loadController(Path(tmp)) : nullptr);
			m_animator_map.insert(animator.entity, m_animators.size());
			m_animators.push(animator);
			m_world.onComponentCreated(animator.entity, ANIMATOR_TYPE, this);
		}
	}

	void setAnimatorUseRootMotion(EntityRef entity, bool value) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (value) animator.flags = Animator::Flags(animator.flags | Animator::USE_ROOT_MOTION);
		else animator.flags = Animator::Flags(animator.flags & ~Animator::USE_ROOT_MOTION);
	}

	bool getAnimatorUseRootMotion(EntityRef entity) override {
		const Animator& animator = m_animators[m_animator_map[entity]];
		return animator.flags & Animator::USE_ROOT_MOTION;
	}

	void setAnimatorSource(EntityRef entity, const Path& path) override
	{
		Animator& animator = m_animators[m_animator_map[entity]];
		unloadResource(animator.resource);
		setSource(animator, path.isEmpty() ? nullptr : loadController(path));
		if (animator.resource && animator.resource->isReady() && m_is_game_running) {
			animator.ctx = animator.resource->createRuntime(animator.default_set);
		}
	}
	
	anim::RuntimeContext* getAnimatorRuntimeContext(EntityRef entity) override {
		const Animator& animator = m_animators[m_animator_map[entity]];
		return animator.ctx;
	}

	anim::Controller* getAnimatorController(EntityRef entity) override {
		const Animator& animator = m_animators[m_animator_map[entity]];
		return animator.resource;
	}

	Path getAnimatorSource(EntityRef entity) override
	{
		const Animator& animator = m_animators[m_animator_map[entity]];
		return animator.resource ? animator.resource->getPath() : Path("");
	}

	bool isPropertyAnimatorEnabled(EntityRef entity) override
	{
		return !isFlagSet(m_property_animators[entity].flags, PropertyAnimator::DISABLED);
	}


	void enablePropertyAnimator(EntityRef entity, bool enabled) override
	{
		PropertyAnimator& animator = m_property_animators[entity];
		setFlag(animator.flags, PropertyAnimator::DISABLED, !enabled);
		animator.time = 0;
		if (!enabled)
		{
			applyPropertyAnimator(entity, animator);
		}
	}


	Path getPropertyAnimation(EntityRef entity) override
	{
		const auto& animator = m_property_animators[entity];
		return animator.animation ? animator.animation->getPath() : Path("");
	}
	
	
	void setPropertyAnimation(EntityRef entity, const Path& path) override
	{
		auto& animator = m_property_animators[entity];
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
		animable.time = Time::fromSeconds(0);
	}


	void updateAnimable(Animable& animable, float time_delta) const
	{
		if (!animable.animation || !animable.animation->isReady()) return;
		EntityRef entity = animable.entity;
		if (!m_world.hasComponent(entity, MODEL_INSTANCE_TYPE)) return;

		Model* model = m_render_module->getModelInstanceModel(entity);
		if (!model || !model->isReady()) return;

		Pose* pose = m_render_module->lockPose(entity);
		if (!pose) return;

		model->getRelativePose(*pose);
		Animation::SampleContext ctx;
		ctx.pose = pose;
		ctx.model = model;
		ctx.time = animable.time;
		animable.animation->getRelativePose(ctx);
		pose->computeAbsolute(*model);

		if (time_delta > 0) {
			Time t = animable.time + Time::fromSeconds(time_delta);
			const Time l = animable.animation->getLength();
			t = t % l;
			animable.time = t;
		} else {
			const Time l = animable.animation->getLength();
			Time dt = Time::fromSeconds(-time_delta) % l;
			Time t = animable.time + l - dt;
			t = t % l;
			animable.time = t;
		}

		m_render_module->unlockPose(entity, true);
	}


	void updateAnimable(EntityRef entity, float time_delta) override
	{
		Animable& animable = m_animables[entity];
		updateAnimable(animable, time_delta);
	}


	void updateAnimator(EntityRef entity, float time_delta) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		updateAnimator(animator, time_delta);
	}

	void setAnimatorInput(EntityRef entity, u32 input_idx, float value) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return;

		if (input_idx >= (u32)animator.resource->m_inputs.size()) return;
		if (animator.resource->m_inputs[input_idx].type != anim::Value::NUMBER) return;

		animator.ctx->inputs[input_idx].f = value;
	}

	void setAnimatorInput(EntityRef entity, u32 input_idx, Vec3 value) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return;

		if (input_idx >= (u32)animator.resource->m_inputs.size()) return;
		if (animator.resource->m_inputs[input_idx].type != anim::Value::VEC3) return;

		animator.ctx->inputs[input_idx].v3 = value;
	}

	void setAnimatorInput(EntityRef entity, u32 input_idx, bool value) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return;

		if (input_idx >= (u32)animator.resource->m_inputs.size()) return;
		if (animator.resource->m_inputs[input_idx].type != anim::Value::BOOL) return;

		animator.ctx->inputs[input_idx].b = value;
	}

	float getAnimatorFloatInput(EntityRef entity, u32 input_idx) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return 0;

		ASSERT(input_idx < (u32)animator.resource->m_inputs.size());
		ASSERT(animator.resource->m_inputs[input_idx].type == anim::Value::NUMBER);

		return animator.ctx->inputs[input_idx].f;
	}

	bool getAnimatorBoolInput(EntityRef entity, u32 input_idx) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return 0;

		ASSERT(input_idx < (u32)animator.resource->m_inputs.size());
		ASSERT(animator.resource->m_inputs[input_idx].type == anim::Value::BOOL);

		return animator.ctx->inputs[input_idx].b;
	}

	Vec3 getAnimatorVec3Input(EntityRef entity, u32 input_idx) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return Vec3(0);

		ASSERT(input_idx < (u32)animator.resource->m_inputs.size());
		ASSERT(animator.resource->m_inputs[input_idx].type == anim::Value::VEC3);

		return animator.ctx->inputs[input_idx].v3;
	}

	LocalRigidTransform getAnimatorRootMotion(EntityRef entity) override
	{
		auto iter = m_animator_map.find(entity);
		if (!iter.isValid()) return {};
		Animator& animator = m_animators[iter.value()];
		return animator.root_motion;
	}


	void applyAnimatorSet(EntityRef entity, u32 idx) override
	{
		Animator& animator = m_animators[m_animator_map[entity]];
		for (auto& entry : animator.resource->m_animation_entries)
		{
			if (entry.set != idx) continue;
			animator.ctx->animations[entry.slot] = entry.animation;
		}
	}


	void setAnimatorDefaultSet(EntityRef entity, u32 idx) override
	{
		Animator& animator = m_animators[m_animator_map[entity]];
		animator.default_set = idx;
	}


	u32 getAnimatorDefaultSet(EntityRef entity) override
	{
		Animator& animator = m_animators[m_animator_map[entity]];
		return animator.default_set;
	}

	OutputMemoryStream& beginBlendstackUpdate(EntityRef entity) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		updateAnimator(animator, 0);
		animator.ctx->blendstack.clear();
		return animator.ctx->blendstack;
	}

	void endBlendstackUpdate(EntityRef entity) override {
		Animator& animator = m_animators[m_animator_map[entity]];

		Model* model = m_render_module->getModelInstanceModel(entity);
		if (!model || !model->isReady()) return;

		Pose* pose = m_render_module->lockPose(entity);
		if (!pose) return;
	
		model->getRelativePose(*pose);
		evalBlendStack(*animator.ctx, *pose);
		pose->computeAbsolute(*model);
		m_render_module->unlockPose(entity, true);
	}

	void updateAnimator(Animator& animator, float time_delta) {
		PROFILE_FUNCTION();
		if (!animator.resource || !animator.resource->isReady()) return;
		if (!animator.ctx) {
			animator.ctx = animator.resource->createRuntime(animator.default_set);
		}

		const EntityRef entity = animator.entity;
		if (!m_world.hasComponent(entity, MODEL_INSTANCE_TYPE)) return;

		Model* model = m_render_module->getModelInstanceModel(entity);
		if (!model || !model->isReady()) return;

		Pose* pose = m_render_module->lockPose(entity);
		if (!pose) return;

		animator.ctx->model = model;
		animator.ctx->time_delta = Time::fromSeconds(time_delta);
		animator.resource->update(*animator.ctx);
		animator.root_motion = animator.ctx->root_motion;

		model->getRelativePose(*pose);
		evalBlendStack(*animator.ctx, *pose);
		
		pose->computeAbsolute(*model);

		m_render_module->unlockPose(entity, true);

		if (animator.flags & Animator::USE_ROOT_MOTION) {
			Transform tr = m_world.getTransform(animator.entity);
			tr.pos += tr.rot.rotate(animator.root_motion.pos);
			tr.rot = animator.root_motion.rot * tr.rot;
			m_world.setTransform(animator.entity, tr);
		}
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
					cmp.module = m_world.getModule(cmp.type);
					cmp.entity = entity;
					ASSERT(curve.property->setter);
					curve.property->set(cmp, -1, v);
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
			if (animator.flags & PropertyAnimator::DISABLED) continue;

			animator.time += time_delta;
			
			applyPropertyAnimator(entity, animator);
		}
	}


	void updateAnimables(float time_delta)
	{
		PROFILE_FUNCTION();
		if (m_animables.size() == 0) return;

		jobs::forEach(m_animables.size(), 1, [&](i32 idx, i32){
			Animable& animable = m_animables.at(idx);
			updateAnimable(animable, time_delta);
		});
	}


	void update(float time_delta) override
	{
		PROFILE_FUNCTION();
		if (!m_is_game_running) return;

		updateAnimables(time_delta);
		updatePropertyAnimators(time_delta);

		jobs::forEach(m_animators.size(), 1, [&](i32 idx, i32){
			updateAnimator(m_animators[idx], time_delta);
		});
	}


	PropertyAnimation* loadPropertyAnimation(const Path& path) const
	{
		if (path.isEmpty()) return nullptr;
		ResourceManagerHub& rm = m_engine.getResourceManager();
		return rm.load<PropertyAnimation>(path);
	}


	Animation* loadAnimation(const Path& path) const
	{
		ResourceManagerHub& rm = m_engine.getResourceManager();
		return rm.load<Animation>(path);
	}


	anim::Controller* loadController(const Path& path) const
	{
		ResourceManagerHub& rm = m_engine.getResourceManager();
		return rm.load<anim::Controller>(path);
	}


	void createPropertyAnimator(EntityRef entity)
	{
		PropertyAnimator& animator = m_property_animators.emplace(entity, m_allocator);
		animator.animation = nullptr;
		animator.time = 0;
		m_world.onComponentCreated(entity, PROPERTY_ANIMATOR_TYPE, this);
	}


	void createAnimable(EntityRef entity)
	{
		Animable& animable = m_animables.insert(entity);
		animable.time = Time::fromSeconds(0);
		animable.animation = nullptr;
		animable.entity = entity;

		m_world.onComponentCreated(entity, ANIMABLE_TYPE, this);
	}


	void createAnimator(EntityRef entity)
	{
		m_animator_map.insert(entity, m_animators.size());
		Animator& animator = m_animators.emplace();
		animator.entity = entity;

		m_world.onComponentCreated(entity, ANIMATOR_TYPE, this);
	}


	ISystem& getSystem() const override { return m_anim_system; }


	IAllocator& m_allocator;
	World& m_world;
	ISystem& m_anim_system;
	Engine& m_engine;
	AssociativeArray<EntityRef, Animable> m_animables;
	AssociativeArray<EntityRef, PropertyAnimator> m_property_animators;
	HashMap<EntityRef, u32> m_animator_map;
	Array<Animator> m_animators;
	RenderModule* m_render_module;
	bool m_is_game_running;
};


UniquePtr<AnimationModule> AnimationModule::create(Engine& engine, ISystem& system, World& world, IAllocator& allocator)
{
	return UniquePtr<AnimationModuleImpl>::create(allocator, engine, system, world, allocator);
}

void AnimationModule::reflect(Engine& engine) {
	LUMIX_MODULE(AnimationModuleImpl, "animation")
		.LUMIX_CMP(PropertyAnimator, "property_animator", "Animation / Property animator")
			.LUMIX_PROP(PropertyAnimation, "Animation").resourceAttribute(PropertyAnimation::TYPE)
			.prop<&AnimationModule::isPropertyAnimatorEnabled, &AnimationModule::enablePropertyAnimator>("Enabled")
		.LUMIX_CMP(Animator, "animator", "Animation / Animator")
			.function<(void (AnimationModule::*)(EntityRef, u32, float))&AnimationModule::setAnimatorInput>("setFloatInput", "AnimationModule::setAnimatorInput")
			.function<(void (AnimationModule::*)(EntityRef, u32, bool))&AnimationModule::setAnimatorInput>("setBoolInput", "AnimationModule::setAnimatorInput")
			.LUMIX_FUNC_EX(AnimationModule::getAnimatorInputIndex, "getInputIndex")
			.LUMIX_PROP(AnimatorSource, "Source").resourceAttribute(anim::Controller::TYPE)
			.LUMIX_PROP(AnimatorDefaultSet, "Default set")
			.LUMIX_PROP(AnimatorUseRootMotion, "Use root motion")
		.LUMIX_CMP(Animable, "animable", "Animation / Animable")
			.LUMIX_PROP(Animation, "Animation").resourceAttribute(Animation::TYPE)
	;
}

} // namespace Lumix
