#pragma once


#include "engine/lumix.h"
#include "engine/plugin.h"
#include "engine/math.h"


#ifdef STATIC_PLUGINS
	#define LUMIX_PHYSICS_API
#elif defined BUILDING_PHYSICS
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_IMPORT
#endif


struct lua_State;


namespace physx
{
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
struct RagdollBone;
struct RigidTransform;
struct Universe;
template <typename T> struct DelegateList;


struct RaycastHit
{
	Vec3 position;
	Vec3 normal;
	EntityPtr entity;
};


struct LUMIX_PHYSICS_API PhysicsScene : IScene
{
	enum class D6Motion : int
	{
		LOCKED,
		LIMITED,
		FREE
	};
	enum class BoneOrientation : int
	{
		X,
		Y
	};
	enum class WheelSlot : int
	{
		FRONT_LEFT,
		FRONT_RIGHT,
		REAR_LEFT,
		REAR_RIGHT
	};
	enum class DynamicType : int
	{
		STATIC,
		DYNAMIC,
		KINEMATIC
	};
	
	struct ContactData
	{
		Vec3 position;
		EntityRef e1;
		EntityRef e2;
	};

	using ContactCallbackHandle = int;

	static PhysicsScene* create(PhysicsSystem& system, Universe& context, Engine& engine, IAllocator& allocator);
	static void destroy(PhysicsScene* scene);
	static void registerLuaAPI(lua_State* L);

	virtual ~PhysicsScene() {}
	virtual void render() = 0;
	virtual EntityPtr raycast(const Vec3& origin, const Vec3& dir, EntityPtr ignore_entity) = 0;
	virtual bool raycastEx(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result, EntityPtr ignored, int layer) = 0;
	virtual PhysicsSystem& getSystem() const = 0;

	virtual DelegateList<void(const ContactData&)>& onContact() = 0;
	virtual void setActorLayer(EntityRef entity, u32 layer) = 0;
	virtual u32 getActorLayer(EntityRef entity) = 0;
	virtual bool getIsTrigger(EntityRef entity) = 0;
	virtual void setIsTrigger(EntityRef entity, bool is_trigger) = 0;
	virtual DynamicType getDynamicType(EntityRef entity) = 0;
	virtual void setDynamicType(EntityRef entity, DynamicType) = 0;
	virtual Path getShapeSource(EntityRef entity) = 0;
	virtual void setShapeSource(EntityRef entity, const Path& str) = 0;
	virtual Path getHeightmapSource(EntityRef entity) = 0;
	virtual void setHeightmapSource(EntityRef entity, const Path& path) = 0;
	virtual float getHeightmapXZScale(EntityRef entity) = 0;
	virtual void setHeightmapXZScale(EntityRef entity, float scale) = 0;
	virtual float getHeightmapYScale(EntityRef entity) = 0;
	virtual void setHeightmapYScale(EntityRef entity, float scale) = 0;
	virtual u32 getHeightfieldLayer(EntityRef entity) = 0;
	virtual void setHeightfieldLayer(EntityRef entity, u32 layer) = 0;
	virtual void updateHeighfieldData(EntityRef entity,
		int x,
		int y,
		int w,
		int h,
		const u8* data,
		int bytes_per_pixel) = 0;

	virtual D6Motion getD6JointXMotion(EntityRef entity) = 0;
	virtual void setD6JointXMotion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointYMotion(EntityRef entity) = 0;
	virtual void setD6JointYMotion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointZMotion(EntityRef entity) = 0;
	virtual void setD6JointZMotion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointSwing1Motion(EntityRef entity) = 0;
	virtual void setD6JointSwing1Motion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointSwing2Motion(EntityRef entity) = 0;
	virtual void setD6JointSwing2Motion(EntityRef entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointTwistMotion(EntityRef entity) = 0;
	virtual void setD6JointTwistMotion(EntityRef entity, D6Motion motion) = 0;
	virtual float getD6JointLinearLimit(EntityRef entity) = 0;
	virtual void setD6JointLinearLimit(EntityRef entity, float limit) = 0;
	virtual Vec2 getD6JointTwistLimit(EntityRef entity) = 0;
	virtual void setD6JointTwistLimit(EntityRef entity, const Vec2& limit) = 0;
	virtual Vec2 getD6JointSwingLimit(EntityRef entity) = 0;
	virtual void setD6JointSwingLimit(EntityRef entity, const Vec2& limit) = 0;
	virtual float getD6JointDamping(EntityRef entity) = 0;
	virtual void setD6JointDamping(EntityRef entity, float value) = 0;
	virtual float getD6JointStiffness(EntityRef entity) = 0;
	virtual void setD6JointStiffness(EntityRef entity, float value) = 0;
	virtual float getD6JointRestitution(EntityRef entity) = 0;
	virtual void setD6JointRestitution(EntityRef entity, float value) = 0;

	virtual float getDistanceJointDamping(EntityRef entity) = 0;
	virtual void setDistanceJointDamping(EntityRef entity, float value) = 0;
	virtual float getDistanceJointStiffness(EntityRef entity) = 0;
	virtual void setDistanceJointStiffness(EntityRef entity, float value) = 0;
	virtual float getDistanceJointTolerance(EntityRef entity) = 0;
	virtual void setDistanceJointTolerance(EntityRef entity, float value) = 0;
	virtual Vec2 getDistanceJointLimits(EntityRef entity) = 0;
	virtual void setDistanceJointLimits(EntityRef entity, const Vec2& value) = 0;
	virtual Vec3 getDistanceJointLinearForce(EntityRef entity) = 0;
	virtual int getJointCount() = 0;
	virtual EntityRef getJointEntity(int index) = 0;

	virtual float getHingeJointDamping(EntityRef entity) = 0;
	virtual void setHingeJointDamping(EntityRef entity, float value) = 0;
	virtual float getHingeJointStiffness(EntityRef entity) = 0;
	virtual void setHingeJointStiffness(EntityRef entity, float value) = 0;
	virtual bool getHingeJointUseLimit(EntityRef entity) = 0;
	virtual void setHingeJointUseLimit(EntityRef entity, bool use_limit) = 0;
	virtual Vec2 getHingeJointLimit(EntityRef entity) = 0;
	virtual void setHingeJointLimit(EntityRef entity, const Vec2& limit) = 0;

	virtual EntityPtr getJointConnectedBody(EntityRef entity) = 0;
	virtual void setJointConnectedBody(EntityRef entity, EntityPtr connected_body) = 0;
	virtual Vec3 getJointAxisPosition(EntityRef entity) = 0;
	virtual void setJointAxisPosition(EntityRef entity, const Vec3& value) = 0;
	virtual Vec3 getJointAxisDirection(EntityRef entity) = 0;
	virtual void setJointAxisDirection(EntityRef entity, const Vec3& value) = 0;
	virtual RigidTransform getJointLocalFrame(EntityRef entity) = 0;
	virtual RigidTransform getJointConnectedBodyLocalFrame(EntityRef entity) = 0;
	virtual physx::PxJoint* getJoint(EntityRef entity) = 0;

	virtual bool getSphericalJointUseLimit(EntityRef entity) = 0;
	virtual void setSphericalJointUseLimit(EntityRef entity, bool use_limit) = 0;
	virtual Vec2 getSphericalJointLimit(EntityRef entity) = 0;
	virtual void setSphericalJointLimit(EntityRef entity, const Vec2& limit) = 0;

	virtual void applyForceToActor(EntityRef entity, const Vec3& force) = 0;
	virtual void applyImpulseToActor(EntityRef entity, const Vec3& force) = 0;
	virtual Vec3 getActorVelocity(EntityRef entity) = 0;
	virtual float getActorSpeed(EntityRef entity) = 0;
	virtual void putToSleep(EntityRef entity) = 0;

	virtual bool isControllerCollisionDown(EntityRef entity) const = 0;
	virtual void moveController(EntityRef entity, const Vec3& v) = 0;
	virtual u32 getControllerLayer(EntityRef entity) = 0;
	virtual void setControllerLayer(EntityRef entity, u32 layer) = 0;
	virtual float getControllerRadius(EntityRef entity) = 0;
	virtual void setControllerRadius(EntityRef entity, float radius) = 0;
	virtual float getControllerHeight(EntityRef entity) = 0;
	virtual void setControllerHeight(EntityRef entity, float height) = 0;
	virtual bool getControllerCustomGravity(EntityRef entity) = 0;
	virtual void setControllerCustomGravity(EntityRef entity, bool gravity) = 0;
	virtual float getControllerCustomGravityAcceleration(EntityRef entity) = 0;
	virtual void setControllerCustomGravityAcceleration(EntityRef entity, float gravityacceleration) = 0;
	virtual bool isControllerTouchingDown(EntityRef entity) = 0;
	virtual void resizeController(EntityRef entity, float height) = 0;

	virtual void addBoxGeometry(EntityRef entity, int index) = 0;
	virtual void removeBoxGeometry(EntityRef entity, int index) = 0;
	virtual int getBoxGeometryCount(EntityRef entity) = 0;
	virtual Vec3 getBoxGeomHalfExtents(EntityRef entity, int index) = 0;
	virtual void setBoxGeomHalfExtents(EntityRef entity, int index, const Vec3& size) = 0;
	virtual Vec3 getBoxGeomOffsetPosition(EntityRef entity, int index) = 0;
	virtual void setBoxGeomOffsetPosition(EntityRef entity, int index, const Vec3& pos) = 0;
	virtual Quat getBoxGeomOffsetRotationQuat(EntityRef entity, int index) = 0;
	virtual Vec3 getBoxGeomOffsetRotation(EntityRef entity, int index) = 0;
	virtual void setBoxGeomOffsetRotation(EntityRef entity, int index, const Vec3& euler_angles) = 0;

	virtual void addSphereGeometry(EntityRef entity, int index) = 0;
	virtual void removeSphereGeometry(EntityRef entity, int index) = 0;
	virtual int getSphereGeometryCount(EntityRef entity) = 0;
	virtual float getSphereGeomRadius(EntityRef entity, int index) = 0;
	virtual void setSphereGeomRadius(EntityRef entity, int index, float size) = 0;
	virtual Vec3 getSphereGeomOffsetPosition(EntityRef entity, int index) = 0;
	virtual void setSphereGeomOffsetPosition(EntityRef entity, int index, const Vec3& pos) = 0;

	virtual BoneOrientation getNewBoneOrientation() const = 0;
	virtual void setNewBoneOrientation(BoneOrientation orientation) = 0;
	virtual RagdollBone* createRagdollBone(EntityRef entity, u32 bone_name_hash) = 0;
	virtual void destroyRagdollBone(EntityRef entity, RagdollBone* bone) = 0;
	virtual physx::PxJoint* getRagdollBoneJoint(RagdollBone* bone) const = 0;
	virtual RagdollBone* getRagdollRootBone(EntityRef entity) const = 0;
	virtual RagdollBone* getRagdollBoneChild(RagdollBone* bone) = 0;
	virtual RagdollBone* getRagdollBoneSibling(RagdollBone* bone) = 0;
	virtual RagdollBone* getRagdollBoneByName(EntityRef entity, u32 bone_name_hash) = 0;
	virtual const char* getRagdollBoneName(RagdollBone* bone) = 0;
	virtual float getRagdollBoneHeight(RagdollBone* bone) = 0;
	virtual float getRagdollBoneRadius(RagdollBone* bone) = 0;
	virtual void setRagdollBoneHeight(RagdollBone* bone, float value) = 0;
	virtual void setRagdollBoneRadius(RagdollBone* bone, float value) = 0;
	virtual RigidTransform getRagdollBoneTransform(RagdollBone* bone) = 0;
	virtual void setRagdollBoneTransform(RagdollBone* bone, const RigidTransform& matrix) = 0;
	virtual void changeRagdollBoneJoint(RagdollBone* child, int type) = 0;
	virtual void getRagdollData(EntityRef entity, OutputMemoryStream& blob) = 0;
	virtual void setRagdollData(EntityRef entity, InputMemoryStream& blob) = 0;
	virtual void setRagdollBoneKinematicRecursive(RagdollBone* bone, bool is_kinematic) = 0;
	virtual void setRagdollBoneKinematic(RagdollBone* bone, bool is_kinematic) = 0;
	virtual bool isRagdollBoneKinematic(RagdollBone* bone) = 0;
	virtual void setRagdollLayer(EntityRef entity, u32 layer) = 0;
	virtual u32 getRagdollLayer(EntityRef entity) = 0;

	virtual float getWheelRadius(EntityRef entity) = 0;
	virtual void setWheelRadius(EntityRef entity, float r) = 0;
	virtual float getWheelWidth(EntityRef entity) = 0;
	virtual void setWheelWidth(EntityRef entity, float w) = 0;
	virtual float getWheelMass(EntityRef entity) = 0;
	virtual void setWheelMass(EntityRef entity, float w) = 0;
	virtual float getWheelMOI(EntityRef entity) = 0;
	virtual void setWheelMOI(EntityRef entity, float moi) = 0;
	virtual WheelSlot getWheelSlot(EntityRef entity) = 0;
	virtual void setWheelSlot(EntityRef entity, WheelSlot s) = 0;

	virtual u32 getDebugVisualizationFlags() const = 0;
	virtual void setDebugVisualizationFlags(u32 flags) = 0;
	virtual void setVisualizationCullingBox(const DVec3& min, const DVec3& max) = 0;

	virtual bool isActorDebugEnabled(EntityRef e) const = 0;
	virtual void enableActorDebug(EntityRef index, bool enable) const = 0;
};


} // namespace Lumix
