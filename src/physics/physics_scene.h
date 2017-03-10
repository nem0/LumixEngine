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
class IAllocator;
struct Matrix;
class Path;
class PhysicsSystem;
struct RagdollBone;
struct Transform;
class Universe;


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
		SPHERE
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

	static PhysicsScene* create(PhysicsSystem& system, Universe& context, Engine& engine, IAllocator& allocator);
	static void destroy(PhysicsScene* scene);
	static void registerLuaAPI(lua_State* L);

	virtual ~PhysicsScene() {}
	virtual void render() = 0;
	virtual Entity raycast(const Vec3& origin, const Vec3& dir) = 0;
	virtual bool raycastEx(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result, Entity ignored) = 0;
	virtual PhysicsSystem& getSystem() const = 0;

	virtual ComponentHandle getActorComponent(Entity entity) = 0;
	virtual void setActorLayer(ComponentHandle cmp, int layer) = 0;
	virtual int getActorLayer(ComponentHandle cmp) = 0;
	virtual DynamicType getDynamicType(ComponentHandle cmp) = 0;
	virtual void setDynamicType(ComponentHandle cmp, DynamicType) = 0;
	virtual Vec3 getHalfExtents(ComponentHandle cmp) = 0;
	virtual void setHalfExtents(ComponentHandle cmp, const Vec3& size) = 0;
	virtual Path getShapeSource(ComponentHandle cmp) = 0;
	virtual void setShapeSource(ComponentHandle cmp, const Path& str) = 0;
	virtual Path getHeightmap(ComponentHandle cmp) = 0;
	virtual void setHeightmap(ComponentHandle cmp, const Path& path) = 0;
	virtual float getHeightmapXZScale(ComponentHandle cmp) = 0;
	virtual void setHeightmapXZScale(ComponentHandle cmp, float scale) = 0;
	virtual float getHeightmapYScale(ComponentHandle cmp) = 0;
	virtual void setHeightmapYScale(ComponentHandle cmp, float scale) = 0;
	virtual int getHeightfieldLayer(ComponentHandle cmp) = 0;
	virtual void setHeightfieldLayer(ComponentHandle cmp, int layer) = 0;
	virtual void updateHeighfieldData(ComponentHandle cmp,
		int x,
		int y,
		int w,
		int h,
		const u8* data,
		int bytes_per_pixel) = 0;

	virtual float getCapsuleRadius(ComponentHandle cmp) = 0;
	virtual void setCapsuleRadius(ComponentHandle cmp, float value) = 0;
	virtual float getCapsuleHeight(ComponentHandle cmp) = 0;
	virtual void setCapsuleHeight(ComponentHandle cmp, float value) = 0;

	virtual float getSphereRadius(ComponentHandle cmp) = 0;
	virtual void setSphereRadius(ComponentHandle cmp, float value) = 0;

	virtual D6Motion getD6JointXMotion(ComponentHandle cmp) = 0;
	virtual void setD6JointXMotion(ComponentHandle cmp, D6Motion motion) = 0;
	virtual D6Motion getD6JointYMotion(ComponentHandle cmp) = 0;
	virtual void setD6JointYMotion(ComponentHandle cmp, D6Motion motion) = 0;
	virtual D6Motion getD6JointZMotion(ComponentHandle cmp) = 0;
	virtual void setD6JointZMotion(ComponentHandle cmp, D6Motion motion) = 0;
	virtual D6Motion getD6JointSwing1Motion(ComponentHandle cmp) = 0;
	virtual void setD6JointSwing1Motion(ComponentHandle cmp, D6Motion motion) = 0;
	virtual D6Motion getD6JointSwing2Motion(ComponentHandle cmp) = 0;
	virtual void setD6JointSwing2Motion(ComponentHandle cmp, D6Motion motion) = 0;
	virtual D6Motion getD6JointTwistMotion(ComponentHandle cmp) = 0;
	virtual void setD6JointTwistMotion(ComponentHandle cmp, D6Motion motion) = 0;
	virtual float getD6JointLinearLimit(ComponentHandle cmp) = 0;
	virtual void setD6JointLinearLimit(ComponentHandle cmp, float limit) = 0;
	virtual Vec2 getD6JointTwistLimit(ComponentHandle cmp) = 0;
	virtual void setD6JointTwistLimit(ComponentHandle cmp, const Vec2& limit) = 0;
	virtual Vec2 getD6JointSwingLimit(ComponentHandle cmp) = 0;
	virtual void setD6JointSwingLimit(ComponentHandle cmp, const Vec2& limit) = 0;

	virtual float getDistanceJointDamping(ComponentHandle cmp) = 0;
	virtual void setDistanceJointDamping(ComponentHandle cmp, float value) = 0;
	virtual float getDistanceJointStiffness(ComponentHandle cmp) = 0;
	virtual void setDistanceJointStiffness(ComponentHandle cmp, float value) = 0;
	virtual float getDistanceJointTolerance(ComponentHandle cmp) = 0;
	virtual void setDistanceJointTolerance(ComponentHandle cmp, float value) = 0;
	virtual Vec2 getDistanceJointLimits(ComponentHandle cmp) = 0;
	virtual void setDistanceJointLimits(ComponentHandle cmp, const Vec2& value) = 0;
	virtual Vec3 getDistanceJointLinearForce(ComponentHandle cmp) = 0;
	virtual int getJointCount() = 0;
	virtual ComponentHandle getJointComponent(int index) = 0;
	virtual Entity getJointEntity(ComponentHandle cmp) = 0;

	virtual float getHingeJointDamping(ComponentHandle cmp) = 0;
	virtual void setHingeJointDamping(ComponentHandle cmp, float value) = 0;
	virtual float getHingeJointStiffness(ComponentHandle cmp) = 0;
	virtual void setHingeJointStiffness(ComponentHandle cmp, float value) = 0;
	virtual bool getHingeJointUseLimit(ComponentHandle cmp) = 0;
	virtual void setHingeJointUseLimit(ComponentHandle cmp, bool use_limit) = 0;
	virtual Vec2 getHingeJointLimit(ComponentHandle cmp) = 0;
	virtual void setHingeJointLimit(ComponentHandle cmp, const Vec2& limit) = 0;

	virtual Entity getJointConnectedBody(ComponentHandle cmp) = 0;
	virtual void setJointConnectedBody(ComponentHandle cmp, Entity entity) = 0;
	virtual Vec3 getJointAxisPosition(ComponentHandle cmp) = 0;
	virtual void setJointAxisPosition(ComponentHandle cmp, const Vec3& value) = 0;
	virtual Vec3 getJointAxisDirection(ComponentHandle cmp) = 0;
	virtual void setJointAxisDirection(ComponentHandle cmp, const Vec3& value) = 0;
	virtual Transform getJointLocalFrame(ComponentHandle cmp) = 0;
	virtual Transform getJointConnectedBodyLocalFrame(ComponentHandle cmp) = 0;
	virtual physx::PxJoint* getJoint(ComponentHandle cmp) = 0;

	virtual bool getSphericalJointUseLimit(ComponentHandle cmp) = 0;
	virtual void setSphericalJointUseLimit(ComponentHandle cmp, bool use_limit) = 0;
	virtual Vec2 getSphericalJointLimit(ComponentHandle cmp) = 0;
	virtual void setSphericalJointLimit(ComponentHandle cmp, const Vec2& limit) = 0;

	virtual void applyForceToActor(ComponentHandle cmp, const Vec3& force) = 0;
	virtual float getActorSpeed(ComponentHandle cmp) = 0;
	virtual void putToSleep(ComponentHandle cmp) = 0;

	virtual void moveController(ComponentHandle cmp, const Vec3& v) = 0;
	virtual ComponentHandle getController(Entity entity) = 0;
	virtual int getControllerLayer(ComponentHandle cmp) = 0;
	virtual void setControllerLayer(ComponentHandle cmp, int layer) = 0;
	virtual float getControllerRadius(ComponentHandle cmp) = 0;
	virtual float getControllerHeight(ComponentHandle cmp) = 0;
	virtual bool isControllerTouchingDown(ComponentHandle cmp) = 0;
	virtual void resizeController(ComponentHandle cmp, float height) = 0;

	virtual BoneOrientation getNewBoneOrientation() const = 0;
	virtual void setNewBoneOrientation(BoneOrientation orientation) = 0;
	virtual RagdollBone* createRagdollBone(ComponentHandle cmp, u32 bone_name_hash) = 0;
	virtual void destroyRagdollBone(ComponentHandle cmp, RagdollBone* bone) = 0;
	virtual physx::PxJoint* getRagdollBoneJoint(RagdollBone* bone) const = 0;
	virtual RagdollBone* getRagdollRootBone(ComponentHandle cmp) const = 0;
	virtual RagdollBone* getRagdollBoneChild(RagdollBone* bone) = 0;
	virtual RagdollBone* getRagdollBoneSibling(RagdollBone* bone) = 0;
	virtual RagdollBone* getRagdollBoneByName(ComponentHandle cmp, u32 bone_name_hash) = 0;
	virtual float getRagdollBoneHeight(RagdollBone* bone) = 0;
	virtual float getRagdollBoneRadius(RagdollBone* bone) = 0;
	virtual void setRagdollBoneHeight(RagdollBone* bone, float value) = 0;
	virtual void setRagdollBoneRadius(RagdollBone* bone, float value) = 0;
	virtual Transform getRagdollBoneTransform(RagdollBone* bone) = 0;
	virtual void setRagdollBoneTransform(RagdollBone* bone, const Transform& matrix) = 0;
	virtual void changeRagdollBoneJoint(RagdollBone* child, int type) = 0;
	virtual void getRagdollData(ComponentHandle cmp, OutputBlob& blob) = 0;
	virtual void setRagdollData(ComponentHandle cmp, InputBlob& blob) = 0;
	virtual void setRagdollBoneKinematicRecursive(RagdollBone* bone, bool is_kinematic) = 0;
	virtual void setRagdollBoneKinematic(RagdollBone* bone, bool is_kinematic) = 0;
	virtual bool isRagdollBoneKinematic(RagdollBone* bone) = 0;
	virtual void setRagdollLayer(ComponentHandle cmp, int layer) = 0;
	virtual int getRagdollLayer(ComponentHandle cmp) = 0;

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
	virtual ComponentHandle getActorComponentHandle(int index) = 0;
};


} // !namespace Lumix
