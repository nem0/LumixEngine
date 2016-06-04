/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_PHYSICS_NX_CLOTH
#define PX_PHYSICS_NX_CLOTH
/** \addtogroup cloth
@{
*/

#include "PxPhysXConfig.h"
#include "PxActor.h"
#include "PxLockedData.h"
#include "PxFiltering.h"
#include "cloth/PxClothFabric.h"
#include "cloth/PxClothTypes.h"
#include "cloth/PxClothCollisionData.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxScene;

/**
\brief Solver configuration parameters for the vertical and horizontal stretch phase types.
\see PxCloth.setStretchConfig()
\see PxClothFabric for information on actual phase data in cloth fabric
*/
struct PxClothStretchConfig
{
	/**
	\brief Stiffness of the stretch constraints.
	\details Defines for the constraint edges how much of the distance error between current length
	and rest length to correct per stiffness period (see PxCloth::setStiffnessFrequency).
	A value of 0 means no correction, a value of 1 corrects to rest length.
	The default is 1.
	*/
	PxReal stiffness;

	/**
	\brief Stiffness multiplier of the cloth solver under certain limits.
	\details The valid range is [0, 1], the default multiplier is 1.0.
	\see stretchLimit
	*/
	PxReal stiffnessMultiplier;

	/**
	\brief Limit to control when stiffnessMultiplier has to be applied.
	\details stiffnessMultiplier multiplies the normal stiffness if the ratio 
	between constraint edge length and rest length lies in the [compressionLimit, 1] range.
	The valid range is [0, 1], the default limit is 1.0.
	*/
	PxReal compressionLimit;

	/**
	\brief Limit to control when stiffnessMultiplier has to be applied.
	\details stiffnessMultiplier multiplies the normal stiffness if the ratio 
	between constraint edge length and rest length lies in the [1, stretchLimit] range.
	The valid range is [1, PX_MAX_F32), the default limit is 1.0.
	*/
	PxReal stretchLimit;

	/**
	\brief Constructor initializes to default values.
	*/
	PX_INLINE PxClothStretchConfig( PxReal stiffness_=1.0f, 
		PxReal stiffnessMultiplier_=1.0f, PxReal compressionLimit_=1.0f, PxReal stretchLimit_=1.0f) 
		: stiffness(stiffness_)
		, stiffnessMultiplier(stiffnessMultiplier_)
		, compressionLimit(compressionLimit_) 
		, stretchLimit(stretchLimit_) 
	{}
};

/**
\brief Solver configuration parameters for the tether phases.
\see PxCloth.setTetherConfig()
\see PxClothFabric for information on actual tether constraints in cloth fabric.
*/
struct PxClothTetherConfig
{
	/**
	\brief Stiffness of the tether constraints.
	\details Defines for the tether constraints how much of the error between current 
	distance and tether length to correct per stiffness period (see PxCloth::setStiffnessFrequency).
	A value of 0 means no correction, a value of 1 corrects to rest length.
	The default stiffness is 1.0.
	*/
	PxReal stiffness;

	/**
	\brief Scale of tether lengths when applying tether constraints.
	\details The limit distance of a tether constraint is computed
	as the product of stretchLimit and the tether length. 
	The default limit is 1.0.
	*/
	PxReal stretchLimit;

	/**
	\brief Constructor sets to default.
	*/
	PX_INLINE PxClothTetherConfig(PxReal stiffness_ = 1.0f, PxReal stretchLimit_ = 1.0f) 
		: stiffness(stiffness_), stretchLimit(stretchLimit_) 
	{}
};

/**
\brief Solver configuration parameters for the tether phases.
\see PxCloth.setTetherConfig()
\see PxClothFabric for information on actual tether constraints in cloth fabric.
*/
struct PxClothMotionConstraintConfig
{
	/**
	\brief Scale of motion constraint radii.
	\details The motion constraint radius is computed
	as constraint.radius * config.scale + config.bias. 
	The default scale is 1.0.
	*/
	PxReal scale;

	/**
	\brief Bias of motion constraint radii.
	\details The motion constraint radius is computed
	as constraint.radius * config.scale + config.bias. 
	The default bias is 0.0.
	*/
	PxReal bias;

	/**
	\brief Stiffness of the motion constraints.
	\details Defines for the motion constraints how much of the error between current 
	distance and constraint radius to correct per stiffness period (see PxCloth::setStiffnessFrequency).
	A value of 0 means no correction, a value of 1 corrects to rest length.
	The default stiffness is 1.
	*/
	PxReal stiffness;

	/**
	\brief Constructor sets to default.
	*/
	PX_INLINE PxClothMotionConstraintConfig(PxReal radiiScale = 1.0f, PxReal radiiBias = 0.0f, PxReal consStiffness = 1.0f) 
		: scale(radiiScale), bias(radiiBias), stiffness(consStiffness) 
	{}
};

/**
\brief Set of connected particles tailored towards simulating character cloth.
\details A cloth object consists of the following components:
\arg A set of particles that sample the cloth. The sampling does not need to be regular.
Particles are simulated in local space, which allows tuning the effect of changes to the global pose on the particles.
\arg Distance, bending, shearing, and tether constraints between particles. 
These are stored in a PxClothFabric instance which can be shared across cloth instances.
\arg Spheres, capsules, convexes, and triangle collision shapes. 
These shapes are all treated separately to the main PhysX rigid body scene.
\arg Virtual particles can be used to improve collision at a finer scale than the cloth sampling.
\arg Motion and separation constraints are used to limit the particle movement within or outside of a sphere.

@see PxPhysics.createCloth
*/
class PxCloth : public PxActor
{
public:
	/**
	\brief Deletes the cloth.
	Do not keep a reference to the deleted instance.
	*/
	virtual void release() = 0;

	/**
	\brief Returns a pointer to the corresponding cloth fabric.
	\return The associated cloth fabric.
	*/
	virtual	PxClothFabric* getFabric() const = 0;

	/**
	\brief Returns world space bounding box.
	\param[in] inflation  Scale factor for computed world bounds. Box extents are multiplied by this value.
	\return Particle bounds in global coordinates.
	*/
	virtual PxBounds3 getWorldBounds(float inflation=1.01f) const = 0;

	/**
	\brief Returns the number of particles.
	\return Number of particles.
	*/
	virtual PxU32 getNbParticles() const = 0;

	/**
	\brief Acquires access to the cloth particle data.
	\details This function returns a pointer to a PxClothParticleData instance providing access to the 
	PxClothParticle array of the current and previous iteration. The user is responsible for calling
	PxClothParticleData::unlock() after reading or updating the data. In case the lock has been 
	requested using PxDataAccessFlag::eWRITABLE, the unlock() call copies the arrays pointed to by
	PxClothParticleData::particles/previousParticles back to the internal particle buffer.
	Updating the data when a read-only lock has been requested results in undefined behavior.
	Requesting multiple concurrent read-only locks is supported, but no other lock may be active
	when requesting a write lock.
	
	If PxDataAccessFlag::eDEVICE is set in flags then the returned pointers will be to GPU 
	device memory, this can be used for direct interop with graphics APIs. Note that these pointers
	should only be considered valid until PxClothParticleData::unlock() is called and should not
	be stored. PxDataAccessFlag::eDEVICE implies read and write access, and changing the 
	particles/previousParticles members results in undefined behavior.

	\param flags Specifies if particle data is read or written.
	\return PxClothParticleData pointer which provides access to positions and weight.
	*/
	virtual PxClothParticleData* lockParticleData(PxDataAccessFlags flags) = 0;
	/**
	\brief Acquires read access to the cloth particle data.
	\return PxClothParticleData pointer which provides access to positions and weight.
	\note This function is equivalent to lockParticleData(PxDataAccessFlag::eREADABLE).
	*/
	virtual	PxClothParticleData* lockParticleData() const = 0;

	/**
	\brief Updates cloth particle location or inverse weight for current and previous particle state.
	\param [in] currentParticles The particle data for the current particle state or NULL if the state should not be changed.
	\param [in] previousParticles The particle data for the previous particle state or NULL if the state should not be changed.
	\note The invWeight stored in \a previousParticles is the new particle inverse mass, or zero for a static particle. 
	However, if invWeight stored in \a currentParticles is non-zero, it is still used once for the next particle integration and fabric solve.
	\note If <b>currentParticles</b> or <b>previousParticles</b> are non-NULL then they must be the length specified by getNbParticles().
	\note This can be used to teleport particles (use same positions for current and previous).
	\see PxClothParticle
	*/
	virtual void setParticles(const PxClothParticle* currentParticles, const PxClothParticle* previousParticles) = 0;

	/**
	\brief Sets cloth flags (e.g. use GPU for simulation, enable CCD, collide against scene).
	\param [in] flag Mask of which flags to set.
	\param [in] value Value to set flags to.
	*/
	virtual	void setClothFlag(PxClothFlag::Enum flag, bool value)	= 0;
	/**
	\brief Set all cloth flags
	\param [in] inFlags Bit mask of flag values
	*/
	virtual	void setClothFlags(PxClothFlags inFlags)	= 0;
	/**
	\brief Returns cloth flags.
	\return Cloth flags.
	*/
	virtual PxClothFlags getClothFlags() const = 0;

	/** @name Integration
	 *  Functions related to particle integration.
	 */
	/// @{

	/**
	\brief Sets pose that the cloth should move to by the end of the next simulate() call. 
	\details This function will move the cloth in world space. The resulting
	simulation may reflect inertia effect as a result of pose acceleration.
	\param [in] pose Target pose at the end of the next simulate() call.
	\see setGlobalPose() to move cloth without inertia effect.
	*/
	virtual void setTargetPose(const PxTransform& pose) = 0;
	/**
	\brief Sets current pose of the cloth without affecting inertia.
	\details Use this to reset the pose (e.g. teleporting).
	\param [in] pose New global pose.
	\note No pose interpolation is performed.
	\note Inertia is not preserved.
	\see setTargetPose() for inertia preserving method.
	*/
	virtual void setGlobalPose(const PxTransform& pose) = 0;
	/**
	\brief Returns global pose.
	\return Global pose as specified by the last setGlobalPose() or setTargetPose() call.
	*/
	virtual PxTransform getGlobalPose() const = 0;

	/**
	\brief Sets the solver frequency parameter.
	\details Solver frequency specifies how often the simulation step is computed per second. 
	For example, a value of 60 represents one simulation step per frame
	in a 60fps scene.  A value of 120 will represent two simulation steps per frame, etc.
	\param [in] frequency Solver frequency per second (default: 60.0).
	*/
	virtual void setSolverFrequency(PxReal frequency) = 0;
	/**
	\brief Returns solver frequency.
	\return Solver frequency.
	*/
	virtual PxReal getSolverFrequency() const = 0;

	/**
	\brief Returns previous time step size.
	\details Time between sampling of previous and current particle positions for computing particle velocity.
	\return Previous time step size.
	*/
	virtual PxReal getPreviousTimeStep() const = 0;

	/**
	\brief Sets the stiffness frequency parameter.
	\details The stiffness frequency controls the power-law nonlinearity of all rate of change parameters
	(stretch stiffness, shear stiffness, bending stiffness, tether stiffness, self-collision 
	stiffness, motion constraint stiffness, damp coefficient, linear and angular drag coefficients). 
	Increasing the frequency avoids numerical cancellation for values near zero or one, but increases the 
	non-linearity of the parameter. It is not recommended to change this parameter after cloth initialization.
	For example, the portion of edge overstretch removed per second is 
	equal to the stretch stiffness raised to the power of the stiffness frequency.
	\param [in] frequency Stiffness frequency per second (default: 10.0).
	*/
	virtual void setStiffnessFrequency(PxReal frequency) = 0;
	/**
	\brief Returns stiffness frequency.
	\return Stiffness frequency.
	\see setStiffnessFrequency() for details.
	*/
	virtual PxReal getStiffnessFrequency() const = 0;

	/**
	\brief Sets the acceleration scale factor to adjust inertia effect from global pose changes.
	\param [in] scale New scale factor between 0.0 (no inertia) and 1.0 (full inertia) (default: 1.0).
	\note The scale is specified independently for each local coordinate axis.
	\note A value of 0.0 disables all inertia effects of translations applied through setTargetPos().
	\see setTargetPose() 
	*/
	virtual void setLinearInertiaScale(PxVec3 scale) = 0;
	/**
	\brief Returns linear acceleration scale parameter.
	\return Linear acceleration scale parameter.
	*/
	virtual PxVec3 getLinearInertiaScale() const = 0;
	/**
	\brief Sets the acceleration scale factor to adjust inertia effect from global pose changes.
	\param [in] scale New scale factor between 0.0 (no inertia) and 1.0 (full inertia) (default: 1.0).
	\note The scale is specified independently for each local rotation axis.
	\note A value of 0.0 disables all inertia effects of rotations applied through setTargetPos().
	\see setTargetPose() 
	*/
	virtual void setAngularInertiaScale(PxVec3 scale) = 0;
	/**
	\brief Returns angular acceleration scale parameter.
	\return Angular acceleration scale parameter.
	*/
	virtual PxVec3 getAngularInertiaScale() const = 0;
	/**
	\brief Sets the acceleration scale factor to adjust inertia effect from global pose changes.
	\param [in] scale New scale factor between 0.0 (no centrifugal force) and 1.0 (full centrifugal force) (default: 1.0).
	\note The scale is specified independently for each local rotation axis.
	\note A value of 0.0 disables all centrifugal forces of rotations applied through setTargetPos().
	\see setTargetPose() 
	*/
	virtual void setCentrifugalInertiaScale(PxVec3 scale) = 0;
	/**
	\brief Returns centrifugal acceleration scale parameter.
	\return Centrifugal acceleration scale parameter.
	*/
	virtual PxVec3 getCentrifugalInertiaScale() const = 0;
	/**
	\brief Same as <code>setLinearInertiaScale(PxVec3(scale)); 
	setAngularInertiaScale(PxVec3(scale)); getCentrifugalInertiaScale(PxVec3(scale)); </code>
	*/
	virtual void setInertiaScale(PxReal scale) = 0;

	/**
	\brief Sets the damping coefficient.
	\details The damping coefficient is the portion of local particle velocity 
	that is canceled per stiffness period (see PxCloth::setStiffnessFrequency).
	\note The scale is specified independently for each local space axis.
	\param [in] dampingCoefficient New damping coefficient between 0.0 and 1.0 (default: 0.0).
	*/
	virtual void setDampingCoefficient(PxVec3 dampingCoefficient) = 0;
	/**
	\brief Returns the damping coefficient.
	\return Damping coefficient.
	*/
	virtual PxVec3 getDampingCoefficient() const = 0;

	/**
	\brief Sets the linear drag coefficient.
	\details The linear drag coefficient is the portion of the pose translation 
	that is applied to each particle per stiffness period (see PxCloth::setStiffnessFrequency).
	\note The scale is specified independently for each local space axis.
	\param [in] dragCoefficient New linear drag coefficient between 0.0f and 1.0 (default: 0.0).
	\note The drag coefficient shouldn't be set higher than the damping coefficient.
	*/
	virtual void setLinearDragCoefficient(PxVec3 dragCoefficient) = 0;
	/**
	\brief Returns the linear drag coefficient.
	\return Linear drag coefficient.
	*/
	virtual PxVec3 getLinearDragCoefficient() const = 0;
	/**
	\brief Sets the angular drag coefficient.
	\details The angular drag coefficient is the portion of the pose rotation 
	that is applied to each particle per stiffness period (see PxCloth::setStiffnessFrequency).
	\note The scale is specified independently for each local rotation axis.
	\param [in] dragCoefficient New angular drag coefficient between 0.0f and 1.0 (default: 0.0).
	\note The drag coefficient shouldn't be set higher than the damping coefficient.
	*/
	virtual void setAngularDragCoefficient(PxVec3 dragCoefficient) = 0;
	/**
	\brief Returns the angular drag coefficient.
	\return Angular drag coefficient.
	*/
	virtual PxVec3 getAngularDragCoefficient() const = 0;
	/**
	\brief Same as <code>setLinearDragCoefficient(PxVec3(coefficient)); 
	setAngularDragCoefficient(PxVec3(coefficient));</code>
	*/
	virtual void setDragCoefficient(PxReal scale) = 0;

	/**
	\brief Sets external particle accelerations.
	\param [in] acceleration New acceleration in global coordinates (default: 0.0).
	\note Use this to implement simple wind etc.
	*/
	virtual void setExternalAcceleration(PxVec3 acceleration) = 0;
	/**
	\brief Returns external acceleration.
	\return External acceleration in global coordinates.
	*/
	virtual PxVec3 getExternalAcceleration() const = 0;

    /**
    \brief Updates particle accelerations, w component is ignored.
	\param [in] particleAccelerations New particle accelerations.
	\note The <b>particleAccelerations</b> must either be null to disable accelerations, 
	or be the same length as the number of particles, see getNbParticles().
	*/
	virtual void setParticleAccelerations(const PxVec4* particleAccelerations) = 0;
	/**
	\brief Copies particle accelerations to the user provided buffer.
	\param [out] particleAccelerationsBuffer Destination buffer, must be at least getNbParticleAccelerations().
	\return true if the copy was successful.
	*/
	virtual bool getParticleAccelerations(PxVec4* particleAccelerationsBuffer) const = 0;
	/**
	\brief Returns the number of particle accelerations.
	\return Number of particle accelerations (same as getNbParticles() if enabled, 0 otherwise).
	*/
	virtual PxU32 getNbParticleAccelerations() const = 0; 

	/// @}

	/** @name Constraints
	 *  Functions related to particle and distance constaints.
	 */
	/// @{

	/**
	\brief Updates motion constraints (position and radius of the constraint sphere).
	\param [in] motionConstraints motion constraints at the end of the next simulate() call.
	\note The <b>motionConstraints</b> must either be null to disable motion constraints, 
	or be the same length as the number of particles, see getNbParticles().
	\see clearInterpolation()
	*/
	virtual void setMotionConstraints(const PxClothParticleMotionConstraint* motionConstraints) = 0;
	/**
	\brief Copies motion constraints to the user provided buffer.
	\param [out] motionConstraintsBuffer Destination buffer, must be at least getNbMotionConstraints().
	\return True if the copy was successful.
	*/
	virtual bool getMotionConstraints(PxClothParticleMotionConstraint* motionConstraintsBuffer) const = 0;
	/**
	\brief Returns the number of motion constraints.
	\return Number of motion constraints (same as getNbParticles() if enabled, 0 otherwise).
	*/
	virtual PxU32 getNbMotionConstraints() const = 0; 
	/**
	\brief Specifies motion constraint scale, bias, and stiffness.
	\param [in] config Motion constraints solver parameters.
	*/
	virtual void setMotionConstraintConfig(const PxClothMotionConstraintConfig& config) = 0;
	/**
	\brief Reads back scale and bias factor for motion constraints.
	\see setMotionConstraintConfig()
	*/
	virtual PxClothMotionConstraintConfig getMotionConstraintConfig() const = 0;

	/**
	\brief Updates separation constraints (position and radius of the constraint sphere).
	\param [in] separationConstraints separation constraints at the end of the next simulate() call.
	\note The <b>separationConstraints</b> must either be null to disable separation constraints, 
	or be the same length as the number of particles, see getNbParticles().
	\see clearInterpolation()
	*/
	virtual void setSeparationConstraints(const PxClothParticleSeparationConstraint* separationConstraints) = 0;
	/**
	\brief Copies separation constraints to the user provided buffer.
	\param [out] separationConstraintsBuffer Destination buffer, must be at least getNbSeparationConstraints().
	\return True if the copy was successful.
	*/
	virtual bool getSeparationConstraints(PxClothParticleSeparationConstraint* separationConstraintsBuffer) const = 0;
	/**
	\brief Returns the number of separation constraints.
	\return Number of separation constraints (same as getNbParticles() if enabled, 0 otherwise).
	*/
	virtual PxU32 getNbSeparationConstraints() const = 0; 

	/**
	\brief Assign current to previous positions for collision shapes, motion constraints, and separation constraints.
	\details This allows to prevent false interpolation after leaping to an animation frame, for example.
	After calling clearInterpolation(), the current positions will be used without interpolation.
	New positions can be set afterwards to interpolate to by the end of the next frame.
    */
	virtual void clearInterpolation() = 0;

	/**
	\brief Sets the solver parameters for the vertical solver phases.
	\param [in] config Stretch solver parameters.
	\param [in] type Type of phases to set config for.
	*/
	virtual void setStretchConfig(PxClothFabricPhaseType::Enum type, const PxClothStretchConfig& config) = 0;
	/**
	\brief Returns the solver parameters for one of the phase types.
	\param [in] type Type of phases to return the config for.
	\return Vertical solver parameters.
	*/
	virtual PxClothStretchConfig getStretchConfig(PxClothFabricPhaseType::Enum type) const = 0;
	/**
	\brief Sets the stiffness parameters for the tether constraints.
	\param [in] config Tether constraints solver parameters.
	*/
	virtual void setTetherConfig(const PxClothTetherConfig& config) = 0;
	/**
	\brief Returns the stiffness parameters for the tether constraints.
	\return Tether solver parameters.
	*/
	virtual PxClothTetherConfig getTetherConfig() const = 0;

	/// @}

	/** @name Collision
	 *  Functions related to particle collision.
	 */
	/// @{

	/**
	\brief Adds a new collision sphere.
	\param [in] sphere New collision sphere.
	\note A maximum of 32 spheres are supported.
	*/
	virtual void addCollisionSphere(const PxClothCollisionSphere& sphere) = 0;
	/**
	\brief Removes collision sphere.
	\param [in] index Index of sphere to remove.
	\note The indices of spheres added after \c index are decremented by 1.
	\note Capsules made from the sphere to be removed are removed as well.
	*/
	virtual void removeCollisionSphere(PxU32 index) = 0;
	/**
	\brief Updates location and radii of collision spheres.
	\param [in] spheresBuffer New sphere positions and radii by the end of the next simulate() call.
	\param [in] count New number of collision spheres.
	\note You can also use this function to change the number of collision spheres.
	\note A maximum of 32 spheres are supported.
	\see clearInterpolation()
	*/
	virtual void setCollisionSpheres(const PxClothCollisionSphere* spheresBuffer, PxU32 count) = 0;
	/**
	\brief Returns the number of collision spheres.
	\return Number of collision spheres.
	*/
	virtual PxU32 getNbCollisionSpheres() const = 0;

	/**
	\brief Adds a new collision capsule. 
	\details A collision capsule is defined as the bounding volume of two spheres.
	\param [in] first Index of first sphere.
	\param [in] second Index of second sphere.
	\note A maximum of 32 capsules are supported.
	\note Spheres referenced by a capsule need to be defined 
	before simulating the scene, see addCollisionSphere/setCollisionSpheres.
	*/
	virtual void addCollisionCapsule(PxU32 first, PxU32 second) = 0;
	/**
	\brief Removes a collision capsule.
	\param [in] index Index of capsule to remove.
	\note The indices of capsules added after \c index are decremented by 1.
	*/
	virtual void removeCollisionCapsule(PxU32 index) = 0;
	/**
	\brief Returns the number of collision capsules.
	\return Number of collision capsules.
	*/
	virtual PxU32 getNbCollisionCapsules() const = 0;

	/**
	\brief Adds a collision plane.
	\param [in] plane New collision plane.
	\note Planes are not used for collision until they are added to a convex object, see addCollisionConvex().
	\note A maximum of 32 planes are supported.
	*/
	virtual void addCollisionPlane(const PxClothCollisionPlane& plane) = 0;
	/**
	\brief Removes a collision plane.
	\param [in] index Index of plane to remove.
	\note The indices of planes added after \c index are decremented by 1.
	\note Convexes that reference the plane will have the plane removed from their mask.
	If after removal a convex consists of zero planes, it will also be removed.
	*/
	virtual void removeCollisionPlane(PxU32 index) = 0;
	/**
	\brief Updates positions of collision planes.
	\param [in] planesBuffer New plane positions by the end of the next simulate() call.
	\param [in] count New number of collision planes.
	\note You can also use this function to change the number of collision planes.
	\note A maximum of 32 planes are supported.
	\see clearInterpolation()
	*/
	virtual void setCollisionPlanes(const PxClothCollisionPlane* planesBuffer, PxU32 count) = 0;
	/**
	\brief Returns the number of collision planes.
	\return Number of collision planes.
	*/
	virtual PxU32 getNbCollisionPlanes() const = 0;

	/**
	\brief Adds a new collision convex.
	\details A collision convex is defined as the intersection of planes.
	\param [in] mask The bitmask of the planes that make up the convex.
	\note Planes referenced by a collision convex need to be defined 
	before simulating the scene, see addCollisionPlane/setCollisionPlanes.
	*/
	virtual void addCollisionConvex(PxU32 mask) = 0;
	/**
	\brief Removes a collision convex.
	\param [in] index Index of convex to remove.
	\note The indices of convexes added after \c index are decremented by 1.
	\note Planes referenced by this convex will not be removed.
	*/
	virtual void removeCollisionConvex(PxU32 index) = 0;
	/**
	\brief Returns the number of collision convexes.
	\return Number of collision convexes.
	*/
	virtual PxU32 getNbCollisionConvexes() const = 0;

	/**
	\brief Adds a new collision triangle.
	\param [in] triangle New collision triangle.
	\note GPU cloth is limited to 500 triangles per instance.
	*/
	virtual void addCollisionTriangle(const PxClothCollisionTriangle& triangle) = 0;
	/**
	\brief Removes a collision triangle. 
	\param [in] index Index of triangle to remove.
	\note The indices of triangles added after \c index are decremented by 1.
	*/
	virtual void removeCollisionTriangle(PxU32 index) = 0;
	/**
	\brief Updates positions of collision triangles.
	\param [in] trianglesBuffer New triangle positions by the end of the next simulate() call.
	\param [in] count New number of collision triangles.
	\note You can also use this function to change the number of collision triangles.
	\note GPU cloth is limited to 500 triangles per instance.
	\see clearInterpolation()
	*/
	virtual void setCollisionTriangles(const PxClothCollisionTriangle* trianglesBuffer, PxU32 count) = 0;
	/**
	\brief Returns the number of collision triangles.
	\return Number of collision triangles.
	*/
	virtual PxU32 getNbCollisionTriangles() const = 0;

	/**
	\brief Retrieves the collision shapes. 
	\details Returns collision spheres, capsules, convexes, and triangles that were added through 
	the addCollision*() methods and modified through the setCollision*() methods.
	\param [out] spheresBuffer Spheres destination buffer, must be NULL or the same length as getNbCollisionSpheres().
	\param [out] capsulesBuffer Capsules destination buffer, must be NULL or the same length as 2*getNbCollisionCapsules().
	\param [out] planesBuffer Planes destination buffer, must be NULL or the same length as getNbCollisionPlanes().
	\param [out] convexesBuffer Convexes destination buffer, must be NULL or the same length as getNbCollisionConvexes().
	\param [out] trianglesBuffer Triangles destination buffer, must be NULL or the same length as getNbCollisionTriangles().
	\note Returns the positions at the end of the next simulate() call as specified by the setCollision*() methods.
	*/
	virtual void getCollisionData(PxClothCollisionSphere* spheresBuffer, PxU32* capsulesBuffer,
		PxClothCollisionPlane* planesBuffer, PxU32* convexesBuffer, PxClothCollisionTriangle* trianglesBuffer) const = 0;

	/**
	\brief Assigns virtual particles.
	\details Virtual particles provide more robust and accurate collision handling against collision spheres and capsules.
	More virtual particles will generally increase the accuracy of collision handling, and thus
	a sufficient number of virtual particles can mimic triangle-based collision handling.\n
	Virtual particles are specified as barycentric interpolation of real particles:
	The position of a virtual particle is w0 * P0 + w1 * P1 + w2 * P2, where P0, P1, P2 real particle positions.
	The barycentric weights w0, w1, w2 are stored in a separate table so they can be shared across multiple virtual particles.
	\param [in] numVirtualParticles total number of virtual particles.
	\param [in] indices Each virtual particle has four indices, the first three for real particle indices, and the last
	for the weight table index.  Thus, the length of <b>indices</b> needs to be 4*numVirtualParticles.
	\param [in] numWeights total number of unique weights triples.
	\param [in] weights array for barycentric weights.
	\note Virtual particles only incur a runtime cost during the collision stage. Still, it is advisable to 
	only use virtual particles for areas where high collision accuracy is desired. (e.g. sleeve around elbow).
	*/
	virtual void setVirtualParticles(PxU32 numVirtualParticles, const PxU32* indices, PxU32 numWeights, const PxVec3* weights) = 0;
	/**
	\brief Returns the number of virtual particles.
	\return Number of virtual particles.
	*/
	virtual PxU32 getNbVirtualParticles() const = 0;
	/**
	\brief Copies index array of virtual particles to the user provided buffer.
	\param [out] indicesBuffer Destination buffer, must be at least 4*getNbVirtualParticles().
	\see setVirtualParticles()
	*/
	virtual void getVirtualParticles(PxU32* indicesBuffer) const = 0;
	/**
	\brief Returns the number of the virtual particle weights.
	\return Number of virtual particle weights.
	*/
	virtual PxU32 getNbVirtualParticleWeights() const = 0;
	/**
	\brief Copies weight table of virtual particles to the user provided buffer.
	\param [out] weightsBuffer Destination buffer, must be at least getNbVirtualParticleWeights().
	*/
	virtual void getVirtualParticleWeights(PxVec3* weightsBuffer) const = 0;

    /**
	\brief Sets the collision friction coefficient.
	\param [in] frictionCoefficient New friction coefficient between 0.0 and 1.0 (default: 0.0).
	\note Currently only spheres and capsules impose friction on the colliding particles.
	*/
	virtual void setFrictionCoefficient(PxReal frictionCoefficient) = 0;
    /**
    \brief Returns the friction coefficient.
	\return Friction coefficient.
     */
	virtual PxReal getFrictionCoefficient() const = 0;

	/**
	\brief Sets the collision mass scaling coefficient.
	\details During collision it is possible to artificially increase the
	mass of a colliding particle, this has an effect comparable to making 
	constraints	attached to the particle stiffer and can help reduce stretching
	and interpenetration around collision shapes. 
	\param [in] scalingCoefficient Unitless multiplier that can take on values > 1 (default: 0.0).
	*/
	virtual void setCollisionMassScale(PxReal scalingCoefficient) = 0;
	/**
	\brief Returns the mass-scaling coefficient.
	\return Mass-scaling coefficient.
	*/
	virtual PxReal getCollisionMassScale() const = 0;

	/// @}

	/** @name Self-Collision
	 *  Functions related to particle against particle collision.
	 */
	/// @{

	/**
	\brief Sets the self collision distance.
	\details A value larger than 0.0 enables particle versus particle collision.
	\param [in] distance Minimum distance at which two particles repel each other (default: 0.0).
	*/
	virtual void setSelfCollisionDistance(PxReal distance) = 0;
	/**
	\brief Returns the self-collision distance.
	\return Self-collision distance.
	*/
	virtual PxReal getSelfCollisionDistance() const = 0;
	/**
	\brief Sets the self collision stiffness.
	\details Self-collision stiffness controls how much two particles repel 
	each other when they are closer than the self-collision distance. 
	\param [in] stiffness Fraction of distance residual to resolve per iteration (default: 1.0).
	*/
	virtual void setSelfCollisionStiffness(PxReal stiffness) = 0;
	/**
	\brief Returns the self-collision stiffness.
	\return Self-collision stiffness.
	*/
	virtual PxReal getSelfCollisionStiffness() const = 0;

	/**
	\brief Sets a subset of cloth particles which participate in self-collision.
	\details If non-null the cloth self-collision will consider a subset of particles
	instead of all the particles. This can be used to improve self-collision performance
	or to increase the minimum distance between two self-colliding particles 
	(and therefore the maximum sensible self-collision distance).
	\param [in] indices array of particle indices which participate in self-collision.
	\param [in] nbIndices number of particle indices, or 0 to use all particles for self-collision.
	\note These indices will also be used if cloth inter-collision is enabled.
	*/
	virtual void setSelfCollisionIndices(const PxU32* indices, PxU32 nbIndices) = 0;
	/**
	\brief Copies array of particles participating in self-collision to the user provided buffer.
	\param [out] indices Destination buffer, must be at least getNbSelfCollisionIndices() in length.
	\return true if the copy was successful.	
	*/
	virtual bool getSelfCollisionIndices(PxU32* indices) const = 0;

	/**
	\brief Returns the number of particles participating in self-collision.
	\return Size of particle subset participating in self-collision, or 0 if all particles are used.
	*/
	virtual PxU32 getNbSelfCollisionIndices() const = 0;

	/**
	\brief Sets the cloth particles rest positions.
	\details If non-null the cloth self-collision will consider the rest positions by
	discarding particle->particle collision where the distance between the associated
	rest particles is < the self collision distance. This allows self-collision distances
	that are larger than the minimum edge length in the mesh. Typically this function
	should be called with the same positions used to construct the cloth instance.
	\param [in] restPositions Undeformed particle positions, the w component will be ignored
	\note <b>restPositions</b> must either be null to disable rest position consideration, 
	or be the same length as the number of particles, see getNbParticles().
	*/
	virtual void setRestPositions(const PxVec4* restPositions) = 0;
	/**
	\brief Copies array of rest positions to the user provided buffer.
	\param [out] restPositions Destination buffer, must be at least getNbParticles() in length.
	\return true if the copy was successful.
	*/
	virtual bool getRestPositions(PxVec4* restPositions) const = 0;
	/**
	\brief Returns the number of rest positions.
	\return Number of rest positions (same as getNbParticles() if enabled, 0 otherwise).
	*/
	virtual PxU32 getNbRestPositions() const = 0; 

	/// @}

	/** @name Inter-Collision
	 *  Functions related to collision between cloth instances.
	 */
	/// @{

	/**
	\brief Sets the user definable collision filter data.
	\param data The data that will be returned in the PxScene filter shader callback.
	\note To disable collision on a cloth actor it is sufficient to set the
	filter data to some non-zero value (if using the SDK's default filter shader).
	@see PxSimulationFilterShader PxFilterData
	*/
	virtual	void setSimulationFilterData(const PxFilterData& data) = 0;

	/**
	\brief Retrieves the object's collision filter data.
	\return Associated filter data
	@see setSimulationFilterData() PxFilterData
	*/
	virtual	PxFilterData getSimulationFilterData() const = 0;

	/// @}

	/** @name Scene-Collision
	 *  Functions related to collision against scene objects.
	 */
	/// @{

	/**
	\brief Sets the width by which the cloth bounding box is increased to find nearby scene collision shapes.
	\details The cloth particles collide against shapes in the scene that intersect the cloth bounding box enlarged by 
	the contact offset (if the eSCENE_COLLISION flag is set). <b>Default:</b> 0.0f
	\param[in] offset <b>Range:</b> [0, PX_MAX_F32)
	@see getContactOffset setRestOffset
	*/
	virtual void setContactOffset(PxReal offset) = 0;

	/**
	\brief Returns cloth contact offset.
	@see setContactOffset
	*/
	virtual PxReal getContactOffset() const = 0;

	/**
	\brief Sets the minimum distance between colliding cloth particles and scene shapes.
	\details Cloth particles colliding against shapes in the scene get no closer to the shape's surface 
	than specified by the rest offset (if the eSCENE_COLLISION flag is set). <b>Default:</b> 0.0f
	\param[in] offset <b>Range:</b> [0, PX_MAX_F32)
	@see getRestOffset setContactOffset
	*/
	virtual void setRestOffset(PxReal offset) = 0;

	/**
	\brief Returns cloth rest offset.
	@see setRestOffset
	*/
	virtual PxReal getRestOffset() const = 0;

	/// @}

	/** @name Sleeping
	 *  Functions related to sleeping.
	 */
	/// @{

	/**
	\brief Sets the velocity threshold for putting cloth in sleep state.
	\details If none of the particles moves faster (in local space)
	than the threshold for a while, the cloth will be put in
	sleep state and simulation will be skipped.
	\param [in] threshold Velocity threshold (default: 0.0f)
	*/
	virtual void setSleepLinearVelocity(PxReal threshold) = 0;
	/**
	\brief Returns the velocity threshold for putting cloth in sleep state.
	\return Velocity threshold for putting cloth in sleep state.
	*/
	virtual PxReal getSleepLinearVelocity() const = 0;
	/**
	\brief Sets the wake counter for the cloth.
	\details The wake counter value determines how long all particles need to	move less than the velocity threshold until the cloth 
	is put to sleep (see #setSleepLinearVelocity()).
	\note Passing in a positive value will wake the cloth up automatically.
	<b>Default:</b> 0.395 (which corresponds to 20 frames for a time step of 0.02)
	\param[in] wakeCounterValue Wake counter value. <b>Range:</b> [0, PX_MAX_F32)
	@see isSleeping() getWakeCounter()
	*/
	virtual void setWakeCounter(PxReal wakeCounterValue) = 0;
	/**
	\brief Returns the wake counter of the cloth.
	\return The wake counter of the cloth.
	@see isSleeping() setWakeCounter()
	*/
	virtual PxReal getWakeCounter() const = 0;
	/**
	\brief Forces cloth to wake up from sleep state.
	\note This will set the wake counter of the cloth to the value specified in #PxSceneDesc::wakeCounterResetValue.
	\note It is invalid to use this method if the cloth has not been added to a scene already.
	@see isSleeping() putToSleep()
	*/
	virtual void wakeUp() = 0;
	/**
	\brief Forces cloth to be put in sleep state.
	\note It is invalid to use this method if the cloth has not been added to a scene already.
	*/
	virtual void putToSleep() = 0;
	/**
	\brief Returns true if cloth is in sleep state
	\note It is invalid to use this method if the cloth has not been added to a scene already.
	\return True if cloth is in sleep state.
	*/
	virtual bool isSleeping() const = 0;

	/// @}

	virtual const char*	getConcreteTypeName() const { return "PxCloth"; }

protected:
	PX_INLINE PxCloth(PxType concreteType, PxBaseFlags baseFlags) : PxActor(concreteType, baseFlags) {}
	PX_INLINE PxCloth(PxBaseFlags baseFlags) : PxActor(baseFlags) {}
	virtual ~PxCloth() {}
	virtual bool isKindOf(const char* name)	const {	return !strcmp("PxCloth", name) || PxActor::isKindOf(name); }
};

PX_DEPRECATED PX_INLINE PxCloth*		PxActor::isCloth()       { return is<PxCloth>(); }
PX_DEPRECATED PX_INLINE const PxCloth*	PxActor::isCloth() const { return is<PxCloth>(); }

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
