#pragma once

#include "core/allocator.h"
#include "core/math.h"

#include "engine/plugin.h"


#ifdef STATIC_PLUGINS
	#define LUMIX_PHYSICS_API
#elif defined BUILDING_PHYSICS
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_IMPORT
#endif


namespace physx {
	class PxJoint;
}


namespace Lumix
{


struct Engine;
struct IAllocator;
struct Matrix;
struct Path;
struct PhysicsSystem;
struct Quat;
struct RigidTransform;
struct World;
template <typename T> struct DelegateList;


struct RaycastHit {
	Vec3 position;
	Vec3 normal;
	EntityPtr entity;
};

struct SweepHit {
	Vec3 position;
	Vec3 normal;
	EntityPtr entity;
	float distance;
};

//@ module PhysicsModule physics "Physics"
struct LUMIX_PHYSICS_API PhysicsModule : IModule {
	//@ enum
	enum class D6Motion : i32 {
		LOCKED,
		LIMITED,
		FREE
	};
	//@ enum
	enum class WheelSlot : i32 {
		FRONT_LEFT,
		FRONT_RIGHT,
		REAR_LEFT,
		REAR_RIGHT
	};
	//@ enum
	enum class DynamicType : i32 {
		STATIC,
		DYNAMIC,
		KINEMATIC
	};
	
	struct ContactData {
		Vec3 position;
		EntityRef e1;
		EntityRef e2;
	};

	static UniquePtr<PhysicsModule> create(PhysicsSystem& system, World& world, Engine& engine, IAllocator& allocator);
	static void reflect();

	virtual ~PhysicsModule() {}
	virtual void forceUpdateDynamicActors(float time_delta) = 0;
	virtual const Array<EntityRef>& getDynamicActors() = 0;
	virtual DelegateList<void(const ContactData&)>& onContact() = 0;
	
	//@ functions
	virtual EntityPtr raycast(const Vec3& origin, const Vec3& dir, float distance, EntityPtr ignore_entity) = 0;
	virtual bool raycastEx(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result, EntityPtr ignored, int layer) = 0;
	virtual bool sweepSphere(const DVec3& pos, float radius, const Vec3& dir, float distance, SweepHit& result, EntityPtr ignored, i32 layer) = 0;
	virtual void setGravity(const Vec3& gravity) = 0;
	//@ end

	virtual void createInstancedMesh(EntityRef entity) = 0;
	virtual void createInstancedCube(EntityRef entity) = 0;
	virtual void createVehicle(EntityRef entity) = 0;
	virtual void createWheel(EntityRef entity) = 0;
	virtual void createActor(EntityRef entity) = 0;
	virtual void createController(EntityRef entity) = 0;
	virtual void createSphericalJoint(EntityRef entity) = 0;
	virtual void createHingeJoint(EntityRef entity) = 0;
	virtual void createDistanceJoint(EntityRef entity) = 0;
	virtual void createD6Joint(EntityRef entity) = 0;
	virtual void createHeightfield(EntityRef entity) = 0;

	virtual void destroyInstancedMesh(EntityRef entity) = 0;
	virtual void destroyInstancedCube(EntityRef entity) = 0;
	virtual void destroyVehicle(EntityRef entity) = 0;
	virtual void destroyWheel(EntityRef entity) = 0;
	virtual void destroyActor(EntityRef entity) = 0;
	virtual void destroyController(EntityRef entity) = 0;
	virtual void destroySphericalJoint(EntityRef entity) = 0;
	virtual void destroyHingeJoint(EntityRef entity) = 0;
	virtual void destroyDistanceJoint(EntityRef entity) = 0;
	virtual void destroyD6Joint(EntityRef entity) = 0;
	virtual void destroyHeightfield(EntityRef entity) = 0;

	//@ component Heightfield physical_heightfield "Heightfield"
	virtual Path getHeightfieldSource(EntityRef entity) = 0;					//@ label "Heightmap" resource_type Texture::TYPE
	virtual void setHeightfieldSource(EntityRef entity, const Path& path) = 0;
	virtual float getHeightfieldXZScale(EntityRef entity) = 0;					//@ label "XZ scale" min 0
	virtual void setHeightfieldXZScale(EntityRef entity, float scale) = 0;
	virtual float getHeightfieldYScale(EntityRef entity) = 0;					//@ min 0
	virtual void setHeightfieldYScale(EntityRef entity, float scale) = 0;
	virtual u32 getHeightfieldLayer(EntityRef entity) = 0;						//@ dynenum Layer
	virtual void setHeightfieldLayer(EntityRef entity, u32 layer) = 0;
	//@ end
	virtual void updateHeighfieldData(EntityRef entity,
		int x,
		int y,
		int w,
		int h,
		const u8* data,
		int bytes_per_pixel) = 0;

	//@ component D6Joint d6_joint "D6 joint"
	virtual D6Motion getD6JointXMotion(EntityRef entity) = 0;
	virtual void setD6JointXMotion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointYMotion(EntityRef entity) = 0;
	virtual void setD6JointYMotion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointZMotion(EntityRef entity) = 0;
	virtual void setD6JointZMotion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointSwing1Motion(EntityRef entity) = 0;					//@ label "Swing 1"
	virtual void setD6JointSwing1Motion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointSwing2Motion(EntityRef entity) = 0;					//@ label "Swing 2"
	virtual void setD6JointSwing2Motion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointTwistMotion(EntityRef entity) = 0;					//@ label "Twist"
	virtual void setD6JointTwistMotion(EntityRef entity, D6Motion motion) = 0;
	virtual float getD6JointLinearLimit(EntityRef entity) = 0;						//@ min 0
	virtual void setD6JointLinearLimit(EntityRef entity, float limit) = 0;
	virtual Vec2 getD6JointTwistLimit(EntityRef entity) = 0;						//@ radians
	virtual void setD6JointTwistLimit(EntityRef entity, const Vec2& limit) = 0;
	virtual Vec2 getD6JointSwingLimit(EntityRef entity) = 0;						//@ radians
	virtual void setD6JointSwingLimit(EntityRef entity, const Vec2& limit) = 0;
	virtual float getD6JointDamping(EntityRef entity) = 0;
	virtual void setD6JointDamping(EntityRef entity, float value) = 0;
	virtual float getD6JointStiffness(EntityRef entity) = 0;
	virtual void setD6JointStiffness(EntityRef entity, float value) = 0;
	virtual float getD6JointRestitution(EntityRef entity) = 0;
	virtual void setD6JointRestitution(EntityRef entity, float value) = 0;
	virtual EntityPtr getD6JointConnectedBody(EntityRef entity) = 0;
	virtual void setD6JointConnectedBody(EntityRef entity, EntityPtr connected_body) = 0;
	virtual Vec3 getD6JointAxisPosition(EntityRef entity) = 0;
	virtual void setD6JointAxisPosition(EntityRef entity, const Vec3& value) = 0;
	virtual Vec3 getD6JointAxisDirection(EntityRef entity) = 0;
	virtual void setD6JointAxisDirection(EntityRef entity, const Vec3& value) = 0;
	//@ end

	//@ component DistanceJoint distance_joint "Distance joint"
	virtual EntityPtr getDistanceJointConnectedBody(EntityRef entity) = 0;
	virtual void setDistanceJointConnectedBody(EntityRef entity, EntityPtr connected_body) = 0;
	virtual Vec3 getDistanceJointAxisPosition(EntityRef entity) = 0;
	virtual void setDistanceJointAxisPosition(EntityRef entity, const Vec3& value) = 0;
	virtual float getDistanceJointDamping(EntityRef entity) = 0;				//@ min 0
	virtual void setDistanceJointDamping(EntityRef entity, float value) = 0;
	virtual float getDistanceJointStiffness(EntityRef entity) = 0;				//@ min 0
	virtual void setDistanceJointStiffness(EntityRef entity, float value) = 0;
	virtual float getDistanceJointTolerance(EntityRef entity) = 0;				//@ min 0
	virtual void setDistanceJointTolerance(EntityRef entity, float value) = 0;
	virtual Vec2 getDistanceJointLimits(EntityRef entity) = 0;
	virtual void setDistanceJointLimits(EntityRef entity, const Vec2& value) = 0;
	virtual Vec3 getDistanceJointLinearForce(EntityRef entity) = 0;
	//@ end

	virtual int getJointCount() = 0;
	virtual EntityRef getJointEntity(int index) = 0;

	//@ component HingeJoint hinge_joint "Hinge joint"
	virtual EntityPtr getHingeJointConnectedBody(EntityRef entity) = 0;
	virtual void setHingeJointConnectedBody(EntityRef entity, EntityPtr connected_body) = 0;
	virtual Vec3 getHingeJointAxisPosition(EntityRef entity) = 0;
	virtual void setHingeJointAxisPosition(EntityRef entity, const Vec3& value) = 0;
	virtual Vec3 getHingeJointAxisDirection(EntityRef entity) = 0;
	virtual void setHingeJointAxisDirection(EntityRef entity, const Vec3& value) = 0;
	virtual float getHingeJointDamping(EntityRef entity) = 0;					//@ min 0
	virtual void setHingeJointDamping(EntityRef entity, float value) = 0;
	virtual float getHingeJointStiffness(EntityRef entity) = 0;					//@ min 0
	virtual void setHingeJointStiffness(EntityRef entity, float value) = 0;
	virtual bool getHingeJointUseLimit(EntityRef entity) = 0;
	virtual void setHingeJointUseLimit(EntityRef entity, bool use_limit) = 0;
	virtual Vec2 getHingeJointLimit(EntityRef entity) = 0;						//@ radians
	virtual void setHingeJointLimit(EntityRef entity, const Vec2& limit) = 0;
	//@ end

	virtual EntityPtr getJointConnectedBody(EntityRef entity) = 0;
	virtual void setJointConnectedBody(EntityRef entity, EntityPtr connected_body) = 0;
	virtual Vec3 getJointAxisPosition(EntityRef entity) = 0;
	virtual void setJointAxisPosition(EntityRef entity, const Vec3& value) = 0;
	virtual Vec3 getJointAxisDirection(EntityRef entity) = 0;
	virtual void setJointAxisDirection(EntityRef entity, const Vec3& value) = 0;
	virtual RigidTransform getJointLocalFrame(EntityRef entity) = 0;
	virtual RigidTransform getJointConnectedBodyLocalFrame(EntityRef entity) = 0;
	virtual physx::PxJoint* getJoint(EntityRef entity) = 0;

	//@ component SphericalJoint spherical_joint "Spherical joint"
	virtual EntityPtr getSphericalJointConnectedBody(EntityRef entity) = 0;
	virtual void setSphericalJointConnectedBody(EntityRef entity, EntityPtr connected_body) = 0;
	virtual Vec3 getSphericalJointAxisPosition(EntityRef entity) = 0;
	virtual void setSphericalJointAxisPosition(EntityRef entity, const Vec3& value) = 0;
	virtual Vec3 getSphericalJointAxisDirection(EntityRef entity) = 0;
	virtual void setSphericalJointAxisDirection(EntityRef entity, const Vec3& value) = 0;
	virtual bool getSphericalJointUseLimit(EntityRef entity) = 0;
	virtual void setSphericalJointUseLimit(EntityRef entity, bool use_limit) = 0;
	virtual Vec2 getSphericalJointLimit(EntityRef entity) = 0;						//@ radians
	virtual void setSphericalJointLimit(EntityRef entity, const Vec2& limit) = 0;
	//@ end

	//@ component Controller physical_controller "Controller"
	virtual float getGravitySpeed(EntityRef entity) const = 0;			//@ function
	virtual void moveController(EntityRef entity, const Vec3& v) = 0;	//@ label "move"
	virtual bool isControllerCollisionDown(EntityRef entity) const = 0; //@ function label "isCollisionDown"
	virtual void resizeController(EntityRef entity, float height) = 0;	//@ label "resize"
	virtual u32 getControllerLayer(EntityRef entity) = 0;				//@ dynenum Layer
	virtual void setControllerLayer(EntityRef entity, u32 layer) = 0;
	virtual float getControllerRadius(EntityRef entity) = 0;
	virtual void setControllerRadius(EntityRef entity, float radius) = 0;
	virtual float getControllerHeight(EntityRef entity) = 0;
	virtual void setControllerHeight(EntityRef entity, float height) = 0;
	virtual bool getControllerCustomGravity(EntityRef entity) = 0;					//@ label "Use custom gravity"
	virtual void setControllerCustomGravity(EntityRef entity, bool gravity) = 0;
	virtual float getControllerCustomGravityAcceleration(EntityRef entity) = 0;
	virtual void setControllerCustomGravityAcceleration(EntityRef entity, float gravityacceleration) = 0;
	virtual bool getControllerUseRootMotion(EntityRef entity) = 0;
	virtual void setControllerUseRootMotion(EntityRef entity, bool enable) = 0;
	//@ end

	//@ component Actor rigid_actor "Actor" icon ICON_FA_VOLLEYBALL_BALL
	virtual void putToSleep(EntityRef entity) = 0;
	virtual void addForceAtPos(EntityRef entity, const Vec3& force, const Vec3& pos) = 0;
	virtual void applyForceToActor(EntityRef entity, const Vec3& force) = 0;				//@ label "applyForce"
	virtual void applyImpulseToActor(EntityRef entity, const Vec3& force) = 0;				//@ label "applyImpulse"
	virtual Vec3 getActorVelocity(EntityRef entity) = 0;
	virtual float getActorSpeed(EntityRef entity) = 0;
	virtual u32 getActorLayer(EntityRef entity) = 0;				//@ dynenum Layer
	virtual void setActorLayer(EntityRef entity, u32 layer) = 0;
	virtual DynamicType getActorDynamicType(EntityRef entity) = 0;			//@ label "Dynamic"
	virtual void setActorDynamicType(EntityRef entity, DynamicType) = 0;
	virtual bool getActorIsTrigger(EntityRef entity) = 0;
	virtual void setActorIsTrigger(EntityRef entity, bool is_trigger) = 0;
	virtual Path getActorMesh(EntityRef entity) = 0;						//@ resource_type PhysicsGeometry::TYPE
	virtual void setActorMesh(EntityRef entity, const Path& path) = 0;
	virtual void setActorMaterial(EntityRef entity, const Path& path) = 0;	//@ resource_type PhysicsMaterial::TYPE
	virtual Path getActorMaterial(EntityRef entity) = 0;
	virtual bool getActorCCD(EntityRef e) = 0;				//@ label "CCD"
	virtual void setActorCCD(EntityRef e, bool is_ccd) = 0;

	//@ array Box boxes
	virtual void addBox(EntityRef entity, int index) = 0;
	virtual void removeBox(EntityRef entity, int index) = 0;
	virtual int getBoxCount(EntityRef entity) = 0;
	virtual Vec3 getBoxHalfExtents(EntityRef entity, int index) = 0;
	virtual void setBoxHalfExtents(EntityRef entity, int index, const Vec3& size) = 0;
	virtual Vec3 getBoxOffsetPosition(EntityRef entity, int index) = 0;			//@ label "Position offset"
	virtual void setBoxOffsetPosition(EntityRef entity, int index, const Vec3& pos) = 0;
	virtual Vec3 getBoxOffsetRotation(EntityRef entity, int index) = 0;			//@ radians label "Rotation offset"
	virtual void setBoxOffsetRotation(EntityRef entity, int index, const Vec3& euler_angles) = 0;
	//@ end
	
	//@ array Sphere spheres
	virtual void addSphere(EntityRef entity, int index) = 0;
	virtual void removeSphere(EntityRef entity, int index) = 0;
	virtual int getSphereCount(EntityRef entity) = 0;
	virtual float getSphereRadius(EntityRef entity, int index) = 0;					//@ min 0
	virtual void setSphereRadius(EntityRef entity, int index, float size) = 0;
	virtual Vec3 getSphereOffsetPosition(EntityRef entity, int index) = 0;			//@ label "Position offset"
	virtual void setSphereOffsetPosition(EntityRef entity, int index, const Vec3& pos) = 0;
	//@ end
	//@ end

	virtual Quat getBoxOffsetRotationQuat(EntityRef entity, int index) = 0;

	//@ component Wheel wheel "Wheel"
	virtual float getWheelSpringStrength(EntityRef entity) = 0;					//@ min 0
	virtual void setWheelSpringStrength(EntityRef entity, float str) = 0;
	virtual float getWheelSpringMaxCompression(EntityRef entity) = 0;			//@ min 0
	virtual void setWheelSpringMaxCompression(EntityRef entity, float str) = 0;
	virtual float getWheelSpringMaxDroop(EntityRef entity) = 0;					//@ min 0
	virtual void setWheelSpringMaxDroop(EntityRef entity, float str) = 0;
	virtual float getWheelSpringDamperRate(EntityRef entity) = 0;				//@ min 0
	virtual void setWheelSpringDamperRate(EntityRef entity, float rate) = 0;
	virtual float getWheelRadius(EntityRef entity) = 0;							//@ min 0
	virtual void setWheelRadius(EntityRef entity, float r) = 0;
	virtual float getWheelWidth(EntityRef entity) = 0;							//@ min 0
	virtual void setWheelWidth(EntityRef entity, float w) = 0;
	virtual float getWheelMass(EntityRef entity) = 0;							//@ min 0
	virtual void setWheelMass(EntityRef entity, float w) = 0;
	virtual float getWheelMOI(EntityRef entity) = 0;							//@ min 0 label "MOI"
	virtual void setWheelMOI(EntityRef entity, float moi) = 0;
	virtual WheelSlot getWheelSlot(EntityRef entity) = 0;
	virtual void setWheelSlot(EntityRef entity, WheelSlot s) = 0;
	virtual float getWheelRPM(EntityRef entity) = 0;							//@ label "RPM"
	//@ end

	//@ component Vehicle vehicle "Vehicle" icon ICON_FA_CAR_ALT
	virtual float getVehiclePeakTorque(EntityRef entity) = 0;
	virtual void setVehiclePeakTorque(EntityRef entity, float value) = 0;
	virtual float getVehicleMaxRPM(EntityRef entity) = 0;					//@ label "Max RPM"
	virtual void setVehicleMaxRPM(EntityRef entity, float value) = 0;
	virtual float getVehicleRPM(EntityRef entity) = 0;						//@ label "RPM"
	virtual i32 getVehicleCurrentGear(EntityRef entity) = 0;
	virtual float getVehicleSpeed(EntityRef entity) = 0;
	virtual void setVehicleAccel(EntityRef entity, float accel) = 0;
	virtual void setVehicleSteer(EntityRef entity, float value) = 0;
	virtual void setVehicleBrake(EntityRef entity, float value) = 0;
	virtual Path getVehicleChassis(EntityRef entity) = 0;	//@ resource_type PhysicsGeometry::TYPE
	virtual void setVehicleChassis(EntityRef entity, const Path& path) = 0;
	virtual float getVehicleMass(EntityRef entity) = 0;						//@ min 0
	virtual void setVehicleMass(EntityRef entity, float mass) = 0;
	virtual float getVehicleMOIMultiplier(EntityRef entity) = 0;			//@ label "MOI multiplier"
	virtual void setVehicleMOIMultiplier(EntityRef entity, float m) = 0;
	virtual Vec3 getVehicleCenterOfMass(EntityRef entity) = 0;
	virtual void setVehicleCenterOfMass(EntityRef entity, Vec3 center) = 0;
	virtual u32 getVehicleWheelsLayer(EntityRef entity) = 0;				//@ dynenum Layer
	virtual void setVehicleWheelsLayer(EntityRef entity, u32 layer) = 0;
	virtual u32 getVehicleChassisLayer(EntityRef entity) = 0;				//@ dynenum Layer
	virtual void setVehicleChassisLayer(EntityRef entity, u32 layer) = 0;
	//@ end

	//@ component InstancedCube physical_instanced_cube "Instanced cube"
	virtual Vec3 getInstancedCubeHalfExtents(EntityRef entity) = 0;
	virtual void setInstancedCubeHalfExtents(EntityRef entity, const Vec3& half_extents) = 0;
	virtual u32 getInstancedCubeLayer(EntityRef entity) = 0;				//@ dynenum Layer
	virtual void setInstancedCubeLayer(EntityRef entity, u32 layer) = 0;
	//@ end

	//@ component InstancedMesh physical_instanced_mesh "Instanced mesh"
	virtual u32 getInstancedMeshLayer(EntityRef entity) = 0;				//@ dynenum Layer
	virtual void setInstancedMeshLayer(EntityRef entity, u32 layer) = 0;
	virtual Path getInstancedMeshGeomPath(EntityRef entity) = 0;			//@ label "Mesh" resource_type PhysicsGeometry::TYPE
	virtual void setInstancedMeshGeomPath(EntityRef entity, const Path& path) = 0;
	//@ end

	virtual u32 getDebugVisualizationFlags() const = 0;
	virtual void setDebugVisualizationFlags(u32 flags) = 0;
	virtual void setVisualizationCullingBox(const DVec3& min, const DVec3& max) = 0;

	virtual bool isActorDebugEnabled(EntityRef e) const = 0;
	virtual void enableActorDebug(EntityRef index, bool enable) const = 0;
};


} // namespace Lumix
