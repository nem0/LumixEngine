#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"
#include "engine/vec.h"


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


class Engine;
struct IAllocator;
struct Matrix;
class Path;
class PhysicsSystem;
struct Quat;
struct RagdollBone;
struct RigidTransform;
class Universe;
template <typename T> class DelegateList;


struct RaycastHit
{
	Vec3 position;
	Vec3 normal;
	Entity entity;
};


class LUMIX_PHYSICS_API PhysicsScene : public IScene
{
public:
	enum class D6Motion : int
	{
		LOCKED,
		LIMITED,
		FREE
	};
	enum class ActorType
	{
		BOX,
		MESH,
		CAPSULE,
		SPHERE,
		RIGID
	};
	enum class BoneOrientation : int
	{
		X,
		Y
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
		Entity e1;
		Entity e2;
	};

	typedef int ContactCallbackHandle;

	static PhysicsScene* create(PhysicsSystem& system, Universe& context, Engine& engine, IAllocator& allocator);
	static void destroy(PhysicsScene* scene);
	static void registerLuaAPI(lua_State* L);

	virtual ~PhysicsScene() {}
	virtual void render() = 0;
	virtual Entity raycast(const Vec3& origin, const Vec3& dir, Entity ignore_entity) = 0;
	virtual bool raycastEx(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result, Entity ignored) = 0;
	virtual PhysicsSystem& getSystem() const = 0;

	virtual DelegateList<void(const ContactData&)>& onContact() = 0;
	virtual void setActorLayer(Entity entity, int layer) = 0;
	virtual int getActorLayer(Entity entity) = 0;
	virtual bool getIsTrigger(Entity entity) = 0;
	virtual void setIsTrigger(Entity entity, bool is_trigger) = 0;
	virtual DynamicType getDynamicType(Entity entity) = 0;
	virtual void setDynamicType(Entity entity, DynamicType) = 0;
	virtual Vec3 getHalfExtents(Entity entity) = 0;
	virtual void setHalfExtents(Entity entity, const Vec3& size) = 0;
	virtual Path getShapeSource(Entity entity) = 0;
	virtual void setShapeSource(Entity entity, const Path& str) = 0;
	virtual Path getHeightmapSource(Entity entity) = 0;
	virtual void setHeightmapSource(Entity entity, const Path& path) = 0;
	virtual float getHeightmapXZScale(Entity entity) = 0;
	virtual void setHeightmapXZScale(Entity entity, float scale) = 0;
	virtual float getHeightmapYScale(Entity entity) = 0;
	virtual void setHeightmapYScale(Entity entity, float scale) = 0;
	virtual int getHeightfieldLayer(Entity entity) = 0;
	virtual void setHeightfieldLayer(Entity entity, int layer) = 0;
	virtual void updateHeighfieldData(Entity entity,
		int x,
		int y,
		int w,
		int h,
		const u8* data,
		int bytes_per_pixel) = 0;

	virtual float getCapsuleRadius(Entity entity) = 0;
	virtual void setCapsuleRadius(Entity entity, float value) = 0;
	virtual float getCapsuleHeight(Entity entity) = 0;
	virtual void setCapsuleHeight(Entity entity, float value) = 0;

	virtual float getSphereRadius(Entity entity) = 0;
	virtual void setSphereRadius(Entity entity, float value) = 0;

	virtual D6Motion getD6JointXMotion(Entity entity) = 0;
	virtual void setD6JointXMotion(Entity entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointYMotion(Entity entity) = 0;
	virtual void setD6JointYMotion(Entity entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointZMotion(Entity entity) = 0;
	virtual void setD6JointZMotion(Entity entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointSwing1Motion(Entity entity) = 0;
	virtual void setD6JointSwing1Motion(Entity entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointSwing2Motion(Entity entity) = 0;
	virtual void setD6JointSwing2Motion(Entity entity, D6Motion motion) = 0;
	virtual D6Motion getD6JointTwistMotion(Entity entity) = 0;
	virtual void setD6JointTwistMotion(Entity entity, D6Motion motion) = 0;
	virtual float getD6JointLinearLimit(Entity entity) = 0;
	virtual void setD6JointLinearLimit(Entity entity, float limit) = 0;
	virtual Vec2 getD6JointTwistLimit(Entity entity) = 0;
	virtual void setD6JointTwistLimit(Entity entity, const Vec2& limit) = 0;
	virtual Vec2 getD6JointSwingLimit(Entity entity) = 0;
	virtual void setD6JointSwingLimit(Entity entity, const Vec2& limit) = 0;
	virtual float getD6JointDamping(Entity entity) = 0;
	virtual void setD6JointDamping(Entity entity, float value) = 0;
	virtual float getD6JointStiffness(Entity entity) = 0;
	virtual void setD6JointStiffness(Entity entity, float value) = 0;
	virtual float getD6JointRestitution(Entity entity) = 0;
	virtual void setD6JointRestitution(Entity entity, float value) = 0;

	virtual float getDistanceJointDamping(Entity entity) = 0;
	virtual void setDistanceJointDamping(Entity entity, float value) = 0;
	virtual float getDistanceJointStiffness(Entity entity) = 0;
	virtual void setDistanceJointStiffness(Entity entity, float value) = 0;
	virtual float getDistanceJointTolerance(Entity entity) = 0;
	virtual void setDistanceJointTolerance(Entity entity, float value) = 0;
	virtual Vec2 getDistanceJointLimits(Entity entity) = 0;
	virtual void setDistanceJointLimits(Entity entity, const Vec2& value) = 0;
	virtual Vec3 getDistanceJointLinearForce(Entity entity) = 0;
	virtual int getJointCount() = 0;
	virtual Entity getJointEntity(int index) = 0;

	virtual float getHingeJointDamping(Entity entity) = 0;
	virtual void setHingeJointDamping(Entity entity, float value) = 0;
	virtual float getHingeJointStiffness(Entity entity) = 0;
	virtual void setHingeJointStiffness(Entity entity, float value) = 0;
	virtual bool getHingeJointUseLimit(Entity entity) = 0;
	virtual void setHingeJointUseLimit(Entity entity, bool use_limit) = 0;
	virtual Vec2 getHingeJointLimit(Entity entity) = 0;
	virtual void setHingeJointLimit(Entity entity, const Vec2& limit) = 0;

	virtual Entity getJointConnectedBody(Entity entity) = 0;
	virtual void setJointConnectedBody(Entity entity, Entity connected_body) = 0;
	virtual Vec3 getJointAxisPosition(Entity entity) = 0;
	virtual void setJointAxisPosition(Entity entity, const Vec3& value) = 0;
	virtual Vec3 getJointAxisDirection(Entity entity) = 0;
	virtual void setJointAxisDirection(Entity entity, const Vec3& value) = 0;
	virtual RigidTransform getJointLocalFrame(Entity entity) = 0;
	virtual RigidTransform getJointConnectedBodyLocalFrame(Entity entity) = 0;
	virtual physx::PxJoint* getJoint(Entity entity) = 0;

	virtual bool getSphericalJointUseLimit(Entity entity) = 0;
	virtual void setSphericalJointUseLimit(Entity entity, bool use_limit) = 0;
	virtual Vec2 getSphericalJointLimit(Entity entity) = 0;
	virtual void setSphericalJointLimit(Entity entity, const Vec2& limit) = 0;

	virtual void applyForceToActor(Entity entity, const Vec3& force) = 0;
	virtual void applyImpulseToActor(Entity entity, const Vec3& force) = 0;
	virtual Vec3 getActorVelocity(Entity entity) = 0;
	virtual float getActorSpeed(Entity entity) = 0;
	virtual void putToSleep(Entity entity) = 0;

	virtual bool isControllerCollisionDown(Entity entity) const = 0;
	virtual void moveController(Entity entity, const Vec3& v) = 0;
	virtual int getControllerLayer(Entity entity) = 0;
	virtual void setControllerLayer(Entity entity, int layer) = 0;
	virtual float getControllerRadius(Entity entity) = 0;
	virtual void setControllerRadius(Entity entity, float radius) = 0;
	virtual float getControllerHeight(Entity entity) = 0;
	virtual void setControllerHeight(Entity entity, float height) = 0;
	virtual bool isControllerTouchingDown(Entity entity) = 0;
	virtual void resizeController(Entity entity, float height) = 0;

	virtual void addBoxGeometry(Entity entity, int index) = 0;
	virtual void removeBoxGeometry(Entity entity, int index) = 0;
	virtual int getBoxGeometryCount(Entity entity) = 0;
	virtual Vec3 getBoxGeomHalfExtents(Entity entity, int index) = 0;
	virtual void setBoxGeomHalfExtents(Entity entity, int index, const Vec3& size) = 0;
	virtual Vec3 getBoxGeomOffsetPosition(Entity entity, int index) = 0;
	virtual void setBoxGeomOffsetPosition(Entity entity, int index, const Vec3& pos) = 0;
	virtual Vec3 getBoxGeomOffsetRotation(Entity entity, int index) = 0;
	virtual void setBoxGeomOffsetRotation(Entity entity, int index, const Vec3& euler_angles) = 0;

	virtual void addSphereGeometry(Entity entity, int index) = 0;
	virtual void removeSphereGeometry(Entity entity, int index) = 0;
	virtual int getSphereGeometryCount(Entity entity) = 0;
	virtual float getSphereGeomRadius(Entity entity, int index) = 0;
	virtual void setSphereGeomRadius(Entity entity, int index, float size) = 0;
	virtual Vec3 getSphereGeomOffsetPosition(Entity entity, int index) = 0;
	virtual void setSphereGeomOffsetPosition(Entity entity, int index, const Vec3& pos) = 0;
	virtual Vec3 getSphereGeomOffsetRotation(Entity entity, int index) = 0;
	virtual void setSphereGeomOffsetRotation(Entity entity, int index, const Vec3& euler_angles) = 0;

	virtual BoneOrientation getNewBoneOrientation() const = 0;
	virtual void setNewBoneOrientation(BoneOrientation orientation) = 0;
	virtual RagdollBone* createRagdollBone(Entity entity, u32 bone_name_hash) = 0;
	virtual void destroyRagdollBone(Entity entity, RagdollBone* bone) = 0;
	virtual physx::PxJoint* getRagdollBoneJoint(RagdollBone* bone) const = 0;
	virtual RagdollBone* getRagdollRootBone(Entity entity) const = 0;
	virtual RagdollBone* getRagdollBoneChild(RagdollBone* bone) = 0;
	virtual RagdollBone* getRagdollBoneSibling(RagdollBone* bone) = 0;
	virtual RagdollBone* getRagdollBoneByName(Entity entity, u32 bone_name_hash) = 0;
	virtual const char* getRagdollBoneName(RagdollBone* bone) = 0;
	virtual float getRagdollBoneHeight(RagdollBone* bone) = 0;
	virtual float getRagdollBoneRadius(RagdollBone* bone) = 0;
	virtual void setRagdollBoneHeight(RagdollBone* bone, float value) = 0;
	virtual void setRagdollBoneRadius(RagdollBone* bone, float value) = 0;
	virtual RigidTransform getRagdollBoneTransform(RagdollBone* bone) = 0;
	virtual void setRagdollBoneTransform(RagdollBone* bone, const RigidTransform& matrix) = 0;
	virtual void changeRagdollBoneJoint(RagdollBone* child, int type) = 0;
	virtual void getRagdollData(Entity entity, OutputBlob& blob) = 0;
	virtual void setRagdollData(Entity entity, InputBlob& blob) = 0;
	virtual void setRagdollBoneKinematicRecursive(RagdollBone* bone, bool is_kinematic) = 0;
	virtual void setRagdollBoneKinematic(RagdollBone* bone, bool is_kinematic) = 0;
	virtual bool isRagdollBoneKinematic(RagdollBone* bone) = 0;
	virtual void setRagdollLayer(Entity entity, int layer) = 0;
	virtual int getRagdollLayer(Entity entity) = 0;

	virtual const char* getCollisionLayerName(int index) = 0;
	virtual void setCollisionLayerName(int index, const char* name) = 0;
	virtual bool canLayersCollide(int layer1, int layer2) = 0;
	virtual void setLayersCanCollide(int layer1, int layer2, bool can_collide) = 0;
	virtual int getCollisionsLayersCount() const = 0;
	virtual void addCollisionLayer() = 0;
	virtual void removeCollisionLayer() = 0;

	virtual u32 getDebugVisualizationFlags() const = 0;
	virtual void setDebugVisualizationFlags(u32 flags) = 0;
	virtual void setVisualizationCullingBox(const Vec3& min, const Vec3& max) = 0;

	virtual int getActorCount() const = 0;
	virtual Entity getActorEntity(int index) = 0;
	virtual ActorType getActorType(int index) = 0;
	virtual bool isActorDebugEnabled(int index) const = 0;
	virtual void enableActorDebug(int index, bool enable) const = 0;
};


} // namespace Lumix
