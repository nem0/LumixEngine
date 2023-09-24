#define LUMIX_NO_CUSTOM_CRT
#include "jolt_physics/jolt_module.h"
#include "jolt_physics/jolt_system.h"
#include "engine/engine.h"
#include "engine/hash_map.h"
#include "engine/profiler.h"
#include "engine/reflection.h"

#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
//#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
//#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
//#include <Jolt/Physics/Body/BodyActivationListener.h>

namespace Lumix {

namespace BroadPhaseLayers
{
	static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
	static constexpr JPH::BroadPhaseLayer MOVING(1);
	static constexpr u32 NUM_LAYERS(2);
};

namespace Layers
{
	static constexpr JPH::ObjectLayer NON_MOVING = 0;
	static constexpr JPH::ObjectLayer MOVING = 1;
	static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};

static const ComponentType BODY_TYPE = reflection::getComponentType("jolt_body");


// An example contact listener
class MyContactListener : public JPH::ContactListener {
public:
	// See: ContactListener
	virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override {
		return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
	}
};


struct JoltModuleImpl : JoltModule {
	struct ObjectVsBroadPhaseLayerFilterImpl final : JPH::ObjectVsBroadPhaseLayerFilter {
		bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
			switch (inLayer1) {
				case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
				case Layers::MOVING: return true;
				default: JPH_ASSERT(false); return false;
			}
		}
	};

	struct ObjectLayerPairFilterImpl final : JPH::ObjectLayerPairFilter {
		bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
			switch (inObject1) {
				case Layers::NON_MOVING: return inObject2 == Layers::MOVING; // Non moving only collides with moving
				case Layers::MOVING: return true;							 // Moving collides with everything
				default: JPH_ASSERT(false); return false;
			}
		}
	};

	struct BPLayerInterfaceImpl final : JPH::BroadPhaseLayerInterface {
		BPLayerInterfaceImpl() {
			// Create a mapping table from object to broad phase layer
			mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
			mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
		}

		virtual u32 GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

		virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
			JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
			return mObjectToBroadPhase[inLayer];
		}

		JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
	};

	struct Body {
		JPH::Body* body = nullptr;
	};

	JoltModuleImpl(World& world, JoltSystem& system)
		: m_world(world)
		, m_system(system)
		, m_bodies(system.getAllocator())
	{
		const u32 max_bodies = 10;
		const u32 max_body_pairs = 10;
		const u32 max_contact_constraints = 10;
		JPH::RegisterDefaultAllocator();
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
		m_jolt_system.Init(max_bodies, 0, max_body_pairs, max_contact_constraints, m_broad_phase_layer_interface, m_object_vs_broadphase_layer_filter, m_object_vs_object_layer_filter);
		static MyContactListener mcl;
		m_jolt_system.SetContactListener(&mcl);
		m_world.entityTransformed().bind<&JoltModuleImpl::onEntityMoved>(this);
	}

	void onEntityMoved(EntityRef e) {
		if (m_update_in_progress) return;

		auto iter = m_bodies.find(e);
		if (!iter.isValid()) return;

		Body& body = iter.value();
		if (!body.body) return;

		const DVec3 pos = m_world.getPosition(e);
		m_jolt_system.GetBodyInterface().SetPosition(body.body->GetID(), {(float)pos.x, (float)pos.y, (float)pos.z}, JPH::EActivation::DontActivate);
	}
	
	void removeSphereGeometry(EntityRef e, i32 idx) {
	}

	void addSphereGeometry(EntityRef e, i32 idx) {
		Body& body = m_bodies[e];
		const DVec3 pos = m_world.getPosition(e);
		if (!body.body) {
			JPH::MutableCompoundShapeSettings s;
			s.SetEmbedded();
			static JPH::SphereShape sphere(0.1f);
			s.AddShape({0, 0, 0}, JPH::Quat::sIdentity(), &sphere);
			JPH::BodyCreationSettings bcs(&s, {(float)pos.x, (float)pos.y, (float)pos.z}, JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::MOVING);
			body.body = m_jolt_system.GetBodyInterface().CreateBody(bcs);
			body.body->SetUserData(e.index);
			m_jolt_system.GetBodyInterface().AddBody(body.body->GetID(), JPH::EActivation::Activate);
			m_jolt_system.GetBodyInterface().SetLinearVelocity(body.body->GetID(), {5, 0, 0});
		}
	}

	u32 getSphereGeometryCount(EntityRef e) {
		Body& body = m_bodies[e];
		if (!body.body) return 0;

		return 0;
	}
	void removeBoxGeometry(EntityRef e, i32 idx) {
	}

	void addBoxGeometry(EntityRef e, i32 idx) {
		Body& body = m_bodies[e];
		const DVec3 pos = m_world.getPosition(e);
		if (!body.body) {
			JPH::MutableCompoundShapeSettings s;
			s.SetEmbedded();
			static JPH::BoxShape box({1, 1, 1});
			s.AddShape({0, 0, 0}, JPH::Quat::sIdentity(), &box);
			JPH::BodyCreationSettings bcs(&s, {(float)pos.x, (float)pos.y, (float)pos.z}, JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
			body.body = m_jolt_system.GetBodyInterface().CreateBody(bcs);
			body.body->SetUserData(e.index);
			m_jolt_system.GetBodyInterface().AddBody(body.body->GetID(), JPH::EActivation::Activate);
		}
	}

	u32 getBoxGeometryCount(EntityRef e) {
		Body& body = m_bodies[e];
		if (!body.body) return 0;

		return 0;
	}

	void destroyRigidActor(EntityRef e) {
		ASSERT(false); // TODO
	}

	void createRigidActor(EntityRef e) {
		m_bodies.insert(e, {});
		m_world.onComponentCreated(e, BODY_TYPE, this);
	}

	void serialize(struct OutputMemoryStream& serializer) override {}
	void deserialize(struct InputMemoryStream& serialize, const struct EntityMap& entity_map, i32 version) override {}

	void update(float time_delta) override {
		if (!m_is_game_running) return;
		m_update_in_progress = true;
		static JPH::TempAllocatorImpl temp_allocator(10 * 1024 * 1024);
		static JPH::JobSystemThreadPool job_system(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 5);
		m_jolt_system.Update(time_delta, 1, &temp_allocator, &job_system);
		JPH::BodyIDVector active_bodies;
		m_jolt_system.GetActiveBodies(JPH::EBodyType::RigidBody, active_bodies);
		for (JPH::BodyID i : active_bodies) {
			JPH::RVec3 pos = m_jolt_system.GetBodyInterface().GetPosition(i);
			EntityRef e = {(i32)m_jolt_system.GetBodyInterface().GetUserData(i)};
			m_world.setPosition(e, {pos.GetX(), pos.GetY(), pos.GetZ()});			
		}
		m_update_in_progress = false;
	}
	
	void stopGame() override {
		m_is_game_running = false;
	}

	void startGame() override {
		m_is_game_running = true;
		
	}

	const char* getName() const override { return "jolt"; }
	ISystem& getSystem() const override { return m_system; }
	World& getWorld() override { return m_world; }

	ISystem& m_system;
	World& m_world;
	JPH::PhysicsSystem m_jolt_system;
	BPLayerInterfaceImpl m_broad_phase_layer_interface;
	ObjectVsBroadPhaseLayerFilterImpl m_object_vs_broadphase_layer_filter;
	ObjectLayerPairFilterImpl m_object_vs_object_layer_filter;
	HashMap<EntityRef, Body> m_bodies;
	bool m_update_in_progress = false;
	bool m_is_game_running = false;
};

void JoltModule::reflect() {
	LUMIX_MODULE(JoltModuleImpl, "jolt")
		.LUMIX_CMP(RigidActor, "jolt_body", "Jolt / Rigid actor")
			.begin_array<&JoltModuleImpl::getBoxGeometryCount, &JoltModuleImpl::addBoxGeometry, &JoltModuleImpl::removeBoxGeometry>("Box geometry")	
			//	.LUMIX_PROP(BoxGeomHalfExtents, "Size")
			//	.LUMIX_PROP(BoxGeomOffsetPosition, "Position offset")
			//	.LUMIX_PROP(BoxGeomOffsetRotation, "Rotation offset").radiansAttribute()
			.end_array()
			.begin_array<&JoltModuleImpl::getSphereGeometryCount, &JoltModuleImpl::addSphereGeometry, &JoltModuleImpl::removeSphereGeometry>("Sphere geometry")
			//	.LUMIX_PROP(SphereGeomRadius, "Radius").minAttribute(0)
			//	.LUMIX_PROP(SphereGeomOffsetPosition, "Position offset")
			.end_array()
		;
}

UniquePtr<JoltModule> JoltModule::create(JoltSystem& system, World& world, struct Engine& engine, struct IAllocator& allocator) {
	return UniquePtr<JoltModuleImpl>::create(allocator, world, system);
}

} // namespace Lumix



