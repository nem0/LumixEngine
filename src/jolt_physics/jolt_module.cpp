#define LUMIX_NO_CUSTOM_CRT
#include "jolt_physics/jolt_module.h"
#include "jolt_physics/jolt_system.h"
#include "engine/engine.h"
#include "engine/hash_map.h"
#include "engine/profiler.h"
#include "engine/reflection.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
//#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
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
static const ComponentType MESH_TYPE = reflection::getComponentType("jolt_mesh");
static const ComponentType BOX_TYPE = reflection::getComponentType("jolt_box");
static const ComponentType SPHERE_TYPE = reflection::getComponentType("jolt_sphere");


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

	struct Mesh { 
		JPH::MeshShape* shape;
	};

	struct Box {
		JPH::BoxShape* shape;
	};

	struct Sphere {
		JPH::SphereShape* shape;
	};

	struct Body {
		JPH::BodyID body;
		JPH::EMotionType motion_type = JPH::EMotionType::Static;
	};

	JoltModuleImpl(World& world, JoltSystem& system)
		: m_world(world)
		, m_system(system)
		, m_bodies(system.getAllocator())
		, m_boxes(system.getAllocator())
		, m_spheres(system.getAllocator())
		, m_meshes(system.getAllocator())
	{
		const u32 max_bodies = 10 * 1024;
		const u32 max_body_pairs = 10 * 1024;
		const u32 max_contact_constraints = 10 * 1024;
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
		if (body.body.IsInvalid()) return;

		const DVec3 pos = m_world.getPosition(e);
		m_jolt_system.GetBodyInterface().SetPosition(body.body, {(float)pos.x, (float)pos.y, (float)pos.z}, JPH::EActivation::DontActivate);
	}
	
	void destroySphere(EntityRef e) {
		delete m_spheres[e].shape;
		m_spheres.erase(e);
		m_world.onComponentDestroyed(e, SPHERE_TYPE, this);
	}

	void createSphere(EntityRef e) {
		const DVec3 pos = m_world.getPosition(e);
		Sphere& sphere = m_spheres.insert(e);
		sphere.shape = new JPH::SphereShape(1);
		m_world.onComponentCreated(e, SPHERE_TYPE, this);
	}

	float getSphereRadius(EntityRef e) {
		return m_spheres[e].shape->GetRadius();
	}

	void setSphereRadius(EntityRef e, float v) {
		delete m_spheres[e].shape;
		m_spheres[e].shape = new JPH::SphereShape(v);
	}

	void destroyBox(EntityRef e) {
		delete m_boxes[e].shape;
		m_boxes.erase(e);
		m_world.onComponentDestroyed(e, BOX_TYPE, this);
	}

	void destroyMesh(EntityRef e) {
		delete m_meshes[e].shape;
		m_meshes.erase(e);
		m_world.onComponentDestroyed(e, MESH_TYPE, this);
	}

	void createMesh(EntityRef e) {
		const DVec3 pos = m_world.getPosition(e);
		Mesh& mesh = m_meshes.insert(e);
		mesh.shape = nullptr;
		m_world.onComponentCreated(e, MESH_TYPE, this);
	}

	void createBox(EntityRef e) {
		const DVec3 pos = m_world.getPosition(e);
		Box& box = m_boxes.insert(e);
		box.shape = new JPH::BoxShape(JPH::Vec3Arg{1, 1, 1}, 0);
		m_world.onComponentCreated(e, BOX_TYPE, this);
	}

	Vec3 getBoxHalfExtents(EntityRef e) {
		return toLumix(m_boxes[e].shape->GetHalfExtent());
	}

	void setBoxHalfExtents(EntityRef e, const Vec3& v) {
		delete m_boxes[e].shape;
		m_boxes[e].shape = new JPH::BoxShape(fromLumix(v), 0);
	}

	void destroyBody(EntityRef e) {
		m_bodies.erase(e);
		m_world.onComponentDestroyed(e, BODY_TYPE, this);
	}

	void createBody(EntityRef e) {
		m_bodies.insert(e, {});
		m_world.onComponentCreated(e, BODY_TYPE, this);
	}

	static JPH::Vec3 fromLumix(const Vec3& v) {
		return {v.x, v.y, v.z};
	}

	static JPH::Vec3 fromLumix(const DVec3& v) {
		return {(float)v.x, (float)v.y, (float)v.z};
	}

	static Vec3 toLumix(const JPH::Vec3& v) {
		return {v.GetX(), v.GetY(), v.GetZ()};
	}

	struct LumixJPHStreamOut : JPH::StreamOut {
		LumixJPHStreamOut(OutputMemoryStream& blob)  : blob(blob) {}

		void WriteBytes(const void* inData, size_t inNumBytes) override { blob.write(inData, inNumBytes); }
		bool IsFailed() const override { return false; }

		OutputMemoryStream& blob;
	};

	struct LumixJPHStreamIn : JPH::StreamIn {
		LumixJPHStreamIn(InputMemoryStream& blob) : blob(blob) {}

		void ReadBytes(void* outData, size_t inNumBytes) override { blob.read(outData, inNumBytes); }
		bool IsEOF() const override { return blob.hasOverflow(); }
		bool IsFailed() const override { return false; }

		InputMemoryStream& blob;
	};

	void serialize(struct OutputMemoryStream& blob) override {
		LumixJPHStreamOut jph_stream(blob);
		JPH::BodyInterface& bodies = m_jolt_system.GetBodyInterface();
		
		blob.write(m_bodies.size());
		for (auto iter = m_bodies.begin(), end = m_bodies.end(); iter != end; ++iter) {
			Body& body = iter.value();
			blob.write(iter.key());
			blob.write(body.motion_type);
		}

		blob.write(m_boxes.size());
		for (auto iter = m_boxes.begin(), end = m_boxes.end(); iter != end; ++iter) {
			blob.write(iter.key());
			JPH::Shape::ShapeToIDMap shape_to_id;
			JPH::Shape::MaterialToIDMap material_to_id;
			iter.value().shape->SaveWithChildren(jph_stream, shape_to_id, material_to_id);
		}

		blob.write(m_spheres.size());
		for (auto iter = m_spheres.begin(), end = m_spheres.end(); iter != end; ++iter) {
			blob.write(iter.key());
			JPH::Shape::ShapeToIDMap shape_to_id;
			JPH::Shape::MaterialToIDMap material_to_id;
			iter.value().shape->SaveWithChildren(jph_stream, shape_to_id, material_to_id);
		}
	}

	void deserialize(struct InputMemoryStream& blob, const struct EntityMap& entity_map, i32 version) override {
		LumixJPHStreamIn jph_stream(blob);
		u32 count = blob.read<u32>();
		for (u32 i = 0; i < count; ++i) {
			EntityRef e;
			blob.read(e);
			e = entity_map.get(e);
			Body& body = m_bodies.insert(e);
			blob.read(body.motion_type);

			m_world.onComponentCreated(e, BODY_TYPE, this);
		}

		count = blob.read<u32>();
		for (u32 i = 0; i < count; ++i) {
			EntityRef e;
			blob.read(e);
			e = entity_map.get(e);
			Box& box = m_boxes.insert(e);

			JPH::Shape::IDToShapeMap id_to_shape;
			JPH::Shape::IDToMaterialMap id_to_material;
			JPH::Shape::ShapeResult res = JPH::Shape::sRestoreWithChildren(jph_stream, id_to_shape, id_to_material);
			ASSERT(res.IsValid());
			box.shape = (JPH::BoxShape*)res.Get().GetPtr();
			box.shape->SetEmbedded();

			m_world.onComponentCreated(e, BOX_TYPE, this);
		}

		count = blob.read<u32>();
		for (u32 i = 0; i < count; ++i) {
			EntityRef e;
			blob.read(e);
			e = entity_map.get(e);
			Sphere& sphere = m_spheres.insert(e);

			JPH::Shape::IDToShapeMap id_to_shape;
			JPH::Shape::IDToMaterialMap id_to_material;
			JPH::Shape::ShapeResult res = JPH::Shape::sRestoreWithChildren(jph_stream, id_to_shape, id_to_material);
			ASSERT(res.IsValid());
			sphere.shape = (JPH::SphereShape*)res.Get().GetPtr();
			sphere.shape->SetEmbedded();

			m_world.onComponentCreated(e, SPHERE_TYPE, this);
		}
	}

	void setBodyMotionType(EntityRef e, JPH::EMotionType v) {
		m_bodies[e].motion_type = v;
	}

	void setBodyVelocity(EntityRef e, const Vec3& velocity) {
		m_jolt_system.GetBodyInterface().SetLinearVelocity(m_bodies[e].body, fromLumix(velocity));
	}

	JPH::EMotionType getBodyMotionType(EntityRef e) {
		return m_bodies[e].motion_type;
	}
	
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
		
		for (auto iter = m_bodies.begin(), end = m_bodies.end(); iter != end; ++iter) {
			Body& body = iter.value();
			EntityRef e = iter.key();
			auto box_iter = m_boxes.find(e);
			JPH::Vec3 pos = fromLumix(m_world.getPosition(e));
			if (box_iter.isValid()) { 
				JPH::BodyCreationSettings bcs(box_iter.value().shape, pos, JPH::Quat::sIdentity(), body.motion_type, Layers::MOVING);
				body.body = m_jolt_system.GetBodyInterface().CreateAndAddBody(bcs, JPH::EActivation::Activate);
				m_jolt_system.GetBodyInterface().SetUserData(body.body, e.index);
				continue;
			}

			auto sphere_iter = m_spheres.find(e);
			if (sphere_iter.isValid()) { 
				JPH::BodyCreationSettings bcs(sphere_iter.value().shape, pos, JPH::Quat::sIdentity(), body.motion_type, Layers::MOVING);
				body.body = m_jolt_system.GetBodyInterface().CreateAndAddBody(bcs, JPH::EActivation::Activate);
				m_jolt_system.GetBodyInterface().SetUserData(body.body, e.index);
				continue;
			}
		}
	}

	const char* getName() const override { return "jolt"; }
	ISystem& getSystem() const override { return m_system; }
	World& getWorld() override { return m_world; }

	JoltSystem& m_system;
	World& m_world;
	JPH::PhysicsSystem m_jolt_system;
	BPLayerInterfaceImpl m_broad_phase_layer_interface;
	ObjectVsBroadPhaseLayerFilterImpl m_object_vs_broadphase_layer_filter;
	ObjectLayerPairFilterImpl m_object_vs_object_layer_filter;
	HashMap<EntityRef, Body> m_bodies;
	HashMap<EntityRef, Box> m_boxes;
	HashMap<EntityRef, Mesh> m_meshes;
	HashMap<EntityRef, Sphere> m_spheres;
	bool m_update_in_progress = false;
	bool m_is_game_running = false;
};

struct MotionTypeEnum : reflection::EnumAttribute {
	u32 count(ComponentUID cmp) const override { return 3; }
	const char* name(ComponentUID cmp, u32 idx) const override { 
		switch ((JPH::EMotionType)idx) {
			case JPH::EMotionType::Dynamic: return "Dynamic";
			case JPH::EMotionType::Static: return "Static";
			case JPH::EMotionType::Kinematic: return "Kinematic";
		}
		ASSERT(false);
		return "N/A";
	}
};

void JoltModule::reflect() {
	LUMIX_MODULE(JoltModuleImpl, "jolt")
		.LUMIX_CMP(Body, "jolt_body", "Jolt / Rigid body")
			.LUMIX_FUNC(setBodyVelocity)
			.LUMIX_ENUM_PROP(BodyMotionType, "Motion type").attribute<MotionTypeEnum>()
		.LUMIX_CMP(Mesh, "jolt_mesh", "Jolt / Mesh")
		.LUMIX_CMP(Box, "jolt_box", "Jolt / Box")
			.LUMIX_PROP(BoxHalfExtents, "Size")
			//	.LUMIX_PROP(BoxGeomOffsetPosition, "Position offset")
			//	.LUMIX_PROP(BoxGeomOffsetRotation, "Rotation offset").radiansAttribute()
		.LUMIX_CMP(Sphere, "jolt_sphere", "Jolt / Sphere")
				.LUMIX_PROP(SphereRadius, "Radius").minAttribute(0)
			//	.LUMIX_PROP(SphereGeomOffsetPosition, "Position offset")
		;
}

UniquePtr<JoltModule> JoltModule::create(JoltSystem& system, World& world, struct Engine& engine, struct IAllocator& allocator) {
	return UniquePtr<JoltModuleImpl>::create(allocator, world, system);
}

} // namespace Lumix



