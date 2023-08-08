#include "animation_module.h"

#include "animation/animation.h"
#include "animation/controller.h"
#include "animation/events.h"
#include "animation/property_animation.h"
#include "engine/associative_array.h"
#include "engine/atomic.h"
#include "engine/engine.h"
#include "engine/hash.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
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

enum class AnimationModuleVersion
{
	LATEST
};


static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType ANIMABLE_TYPE = reflection::getComponentType("animable");
static const ComponentType PROPERTY_ANIMATOR_TYPE = reflection::getComponentType("property_animator");
static const ComponentType ANIMATOR_TYPE = reflection::getComponentType("animator");


struct AnimationModuleImpl final : AnimationModule
{
	friend struct AnimationSystemImpl;
	
	struct Animator
	{
		EntityRef entity;
		anim::Controller* resource = nullptr;
		u32 default_set = 0;
		anim::RuntimeContext* ctx = nullptr;
		LocalRigidTransform root_motion = {{0, 0, 0}, {0, 0, 0, 1}};

		struct IK {
			float weight = 0;
			Vec3 target;
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


	const OutputMemoryStream* getEventStream(EntityRef entity) const override {
		auto iter = m_animator_map.find(entity);
		const Animator& animator = m_animators[iter.value()];
		return animator.ctx ? &animator.ctx->events : nullptr;
	}


	void setAnimatorIK(EntityRef entity, u32 index, float weight, const Vec3& target) override {
		auto iter = m_animator_map.find(entity);
		Animator& animator = m_animators[iter.value()];
		Animator::IK& ik = animator.inverse_kinematics[index];
		ik.weight = clamp(weight, 0.f, 1.f);
		ik.target = target;
	}


	int getAnimatorInputIndex(EntityRef entity, const char* name) const override
	{
		const Animator& animator = m_animators[m_animator_map[entity]];
		anim::InputDecl& decl = animator.resource->m_inputs;
		for (u32 i = 0; i < lengthOf(decl.inputs); ++i) {
			if (decl.inputs[i].type != anim::InputDecl::EMPTY && equalStrings(decl.inputs[i].name, name)) return i;
		}
		return -1;
	}


	void setAnimatorFloatInput(EntityRef entity, u32 input_idx, float value)
	{
		auto iter = m_animator_map.find(entity);
		if (!iter.isValid()) return;

		Animator& animator = m_animators[iter.value()];
		const anim::InputDecl& decl = animator.resource->m_inputs;
		if (input_idx >= decl.inputs_count) return;
		if (!animator.ctx) return;

		if (decl.inputs[input_idx].type == anim::InputDecl::FLOAT) {
			memcpy(&animator.ctx->inputs[decl.inputs[input_idx].offset], &value, sizeof(value));
		}
		else {
			logWarning("Trying to set float to ", decl.inputs[input_idx].name);
		}
	}


	void setAnimatorU32Input(EntityRef entity, u32 input_idx, u32 value)
	{
		auto iter = m_animator_map.find(entity);
		if (!iter.isValid()) return;

		Animator& animator = m_animators[iter.value()];
		const anim::InputDecl& decl = animator.resource->m_inputs;
		if (input_idx >= decl.inputs_count) return;
		if (!animator.ctx) return;

		if (decl.inputs[input_idx].type == anim::InputDecl::U32) {
			*(u32*)&animator.ctx->inputs[decl.inputs[input_idx].offset] = value;
		}
		else {
			logWarning("Trying to set int to ", decl.inputs[input_idx].name);
		}
	}


	void setAnimatorBoolInput(EntityRef entity, u32 input_idx, bool value)
	{
		auto iter = m_animator_map.find(entity);
		if (!iter.isValid()) return;

		Animator& animator = m_animators[iter.value()];
		const anim::InputDecl& decl = animator.resource->m_inputs;
		if (input_idx >= decl.inputs_count) return;
		if (!animator.ctx) return;

		if (decl.inputs[input_idx].type == anim::InputDecl::BOOL) {
			*(bool*)&animator.ctx->inputs[decl.inputs[input_idx].offset] = value;
		}
		else {
			logWarning("Trying to set bool to ", decl.inputs[input_idx].name);
		}
	}


	float getAnimationLength(int animation_idx) override
	{
		auto* animation = static_cast<Animation*>(animation_idx > 0 ? m_engine.getLuaResource(animation_idx) : nullptr);
		if (animation) return animation->getLength().seconds();
		return 0;
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
			animator.entity = entity_map.get(animator.entity);

			const char* tmp = serializer.readString();
			setSource(animator, tmp[0] ? loadController(Path(tmp)) : nullptr);
			m_animator_map.insert(animator.entity, m_animators.size());
			m_animators.push(animator);
			m_world.onComponentCreated(animator.entity, ANIMATOR_TYPE, this);
		}
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
		return !m_property_animators[entity].flags.isSet(PropertyAnimator::DISABLED);
	}


	void enablePropertyAnimator(EntityRef entity, bool enabled) override
	{
		PropertyAnimator& animator = m_property_animators[entity];
		animator.flags.set(PropertyAnimator::DISABLED, !enabled);
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
		if (!model->isReady()) return;

		Pose* pose = m_render_module->lockPose(entity);
		if (!pose) return;

		model->getRelativePose(*pose);
		animable.animation->getRelativePose(animable.time, *pose, *model, nullptr);
		pose->computeAbsolute(*model);

		Time t = animable.time + Time::fromSeconds(time_delta);
		const Time l = animable.animation->getLength();
		t = t % l;
		animable.time = t;

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

		const anim::InputDecl& decl = animator.resource->m_inputs;
		if (input_idx >= decl.inputs_count) return;
		if (decl.inputs[input_idx].type != anim::InputDecl::FLOAT) return;

		*(float*)&animator.ctx->inputs[decl.inputs[input_idx].offset] = value;
	}

	void setAnimatorInput(EntityRef entity, u32 input_idx, bool value) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return;

		const anim::InputDecl& decl = animator.resource->m_inputs;
		if (input_idx >= decl.inputs_count) return;
		if (decl.inputs[input_idx].type != anim::InputDecl::BOOL) return;

		*(bool*)&animator.ctx->inputs[decl.inputs[input_idx].offset] = value;
	}

	float getAnimatorFloatInput(EntityRef entity, u32 input_idx) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return 0;

		const anim::InputDecl& decl = animator.resource->m_inputs;
		ASSERT(input_idx < lengthOf(decl.inputs));
		ASSERT(decl.inputs[input_idx].type == anim::InputDecl::FLOAT);

		return *(float*)&animator.ctx->inputs[decl.inputs[input_idx].offset];
	}

	bool getAnimatorBoolInput(EntityRef entity, u32 input_idx) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return 0;

		const anim::InputDecl& decl = animator.resource->m_inputs;
		ASSERT(input_idx < lengthOf(decl.inputs));
		ASSERT(decl.inputs[input_idx].type == anim::InputDecl::BOOL);

		return *(bool*)&animator.ctx->inputs[decl.inputs[input_idx].offset];
	}

	u32 getAnimatorU32Input(EntityRef entity, u32 input_idx) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return 0;

		const anim::InputDecl& decl = animator.resource->m_inputs;
		ASSERT(input_idx < lengthOf(decl.inputs));
		ASSERT(decl.inputs[input_idx].type == anim::InputDecl::U32);

		return *(u32*)&animator.ctx->inputs[decl.inputs[input_idx].offset];
	}

	void setAnimatorInput(EntityRef entity, u32 input_idx, u32 value) override {
		Animator& animator = m_animators[m_animator_map[entity]];
		if (!animator.ctx) return;

		const anim::InputDecl& decl = animator.resource->m_inputs;
		if (input_idx >= decl.inputs_count) return;
		if (decl.inputs[input_idx].type != anim::InputDecl::U32) return;

		*(u32*)&animator.ctx->inputs[decl.inputs[input_idx].offset] = value;
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

	void updateAnimator(Animator& animator, float time_delta)
	{
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
		animator.ctx->root_bone_hash = BoneNameHash(animator.resource->m_root_motion_bone);
		animator.resource->update(*animator.ctx, animator.root_motion);

		model->getRelativePose(*pose);
		animator.resource->getPose(*animator.ctx, *pose);
		
		for (Animator::IK& ik : animator.inverse_kinematics) {
			if (ik.weight == 0) break;
			const u32 idx = u32(&ik - animator.inverse_kinematics);
			updateIK(animator.resource->m_ik[idx], ik, *pose, *model);
		}

		pose->computeAbsolute(*model);

		m_render_module->unlockPose(entity, true);
	}

	static LocalRigidTransform getAbsolutePosition(const Pose& pose, const Model& model, int bone_index)
	{
		const Model::Bone& bone = model.getBone(bone_index);
		LocalRigidTransform bone_transform{pose.positions[bone_index], pose.rotations[bone_index]};
		if (bone.parent_idx < 0)
		{
			return bone_transform;
		}
		return getAbsolutePosition(pose, model, bone.parent_idx) * bone_transform;
	}

	static void updateIK(anim::Controller::IK& res_ik, Animator::IK& ik, Pose& pose, Model& model)
	{
		u32 indices[anim::Controller::IK::MAX_BONES_COUNT];
		LocalRigidTransform transforms[anim::Controller::IK::MAX_BONES_COUNT];
		Vec3 old_pos[anim::Controller::IK::MAX_BONES_COUNT];
		float len[anim::Controller::IK::MAX_BONES_COUNT - 1];
		float len_sum = 0;
		for (int i = 0; i < res_ik.bones_count; ++i) {
			auto iter = model.getBoneIndex(res_ik.bones[i]);
			if (!iter.isValid()) return;

			indices[i] = iter.value();
		}

		// convert from bone space to object space
		const Model::Bone& first_bone = model.getBone(indices[0]);
		LocalRigidTransform roots_parent;
		if (first_bone.parent_idx >= 0) {
			roots_parent = getAbsolutePosition(pose, model, first_bone.parent_idx);
		}
		else {
			roots_parent.pos = Vec3::ZERO;
			roots_parent.rot = Quat::IDENTITY;
		}

		LocalRigidTransform parent_tr = roots_parent;
		for (int i = 0; i < res_ik.bones_count; ++i) {
			LocalRigidTransform tr{pose.positions[indices[i]], pose.rotations[indices[i]]};
			transforms[i] = parent_tr * tr;
			old_pos[i] = transforms[i].pos;
			if (i > 0) {
				len[i - 1] = length(transforms[i].pos - transforms[i - 1].pos);
				len_sum += len[i - 1];
			}
			parent_tr = transforms[i];
		}

		Vec3 target = ik.target;
		Vec3 to_target = target - transforms[0].pos;
		if (len_sum * len_sum < squaredLength(to_target)) {
			to_target = normalize(to_target);
			target = transforms[0].pos + to_target * len_sum;
		}

		for (int iteration = 0; iteration < res_ik.max_iterations; ++iteration) {
			transforms[res_ik.bones_count - 1].pos = target;
			
			for (int i = res_ik.bones_count - 1; i > 1; --i) {
				Vec3 dir = normalize((transforms[i - 1].pos - transforms[i].pos));
				transforms[i - 1].pos = transforms[i].pos + dir * len[i - 1];
			}

			for (int i = 1; i < res_ik.bones_count; ++i) {
				Vec3 dir = normalize((transforms[i].pos - transforms[i - 1].pos));
				transforms[i].pos = transforms[i - 1].pos + dir * len[i - 1];
			}
		}

		// compute rotations from new positions
		for (int i = res_ik.bones_count - 2; i >= 0; --i) {
			Vec3 old_d = old_pos[i + 1] - old_pos[i];
			Vec3 new_d = transforms[i + 1].pos - transforms[i].pos;

			Quat rel_rot = Quat::vec3ToVec3(old_d, new_d);
			transforms[i].rot = rel_rot * transforms[i].rot;
		}

		// convert from object space to bone space
		LocalRigidTransform ik_out[anim::Controller::IK::MAX_BONES_COUNT];
		for (int i = res_ik.bones_count - 1; i > 0; --i) {
			transforms[i] = transforms[i - 1].inverted() * transforms[i];
			ik_out[i].pos = transforms[i].pos;
		}
		for (int i = res_ik.bones_count - 2; i > 0; --i) {
			ik_out[i].rot = transforms[i].rot;
		}
		ik_out[res_ik.bones_count - 1].rot = pose.rotations[indices[res_ik.bones_count - 1]];

		if (first_bone.parent_idx >= 0) {
			ik_out[0].rot = roots_parent.rot.conjugated() * transforms[0].rot;
		}
		else {
			ik_out[0].rot = transforms[0].rot;
		}
		ik_out[0].pos = pose.positions[indices[0]];

		const float w = ik.weight;
		for (u32 i = 0; i < res_ik.bones_count; ++i) {
			const u32 idx = indices[i];
			pose.positions[idx] = lerp(pose.positions[idx], ik_out[i].pos, w);
			pose.rotations[idx] = nlerp(pose.rotations[idx], ik_out[i].rot, w);
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
			if (animator.flags.isSet(PropertyAnimator::DISABLED)) continue;

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
			.function<(void (AnimationModule::*)(EntityRef, u32, u32))&AnimationModule::setAnimatorInput>("setU32Input", "AnimationModule::setAnimatorInput")
			.function<(void (AnimationModule::*)(EntityRef, u32, float))&AnimationModule::setAnimatorInput>("setFloatInput", "AnimationModule::setAnimatorInput")
			.function<(void (AnimationModule::*)(EntityRef, u32, bool))&AnimationModule::setAnimatorInput>("setBoolInput", "AnimationModule::setAnimatorInput")
			.LUMIX_FUNC_EX(AnimationModule::getAnimatorInputIndex, "getInputIndex")
			.LUMIX_FUNC_EX(AnimationModule::setAnimatorIK, "setIK")
			.LUMIX_PROP(AnimatorSource, "Source").resourceAttribute(anim::Controller::TYPE)
			.LUMIX_PROP(AnimatorDefaultSet, "Default set")
		.LUMIX_CMP(Animable, "animable", "Animation / Animable")
			.LUMIX_PROP(Animation, "Animation").resourceAttribute(Animation::TYPE)
	;
}

} // namespace Lumix
