// This code contains NVIDIA Confidential Information and is disclosed to you 
// under a form of NVIDIA software license agreement provided separately to you.
//
// Notice
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software and related documentation and 
// any modifications thereto. Any use, reproduction, disclosure, or 
// distribution of this software and related documentation without an express 
// license agreement from NVIDIA Corporation is strictly prohibited.
// 
// ALL NVIDIA DESIGN SPECIFICATIONS, CODE ARE PROVIDED "AS IS.". NVIDIA MAKES
// NO WARRANTIES, EXPRESSED, IMPLIED, STATUTORY, OR OTHERWISE WITH RESPECT TO
// THE MATERIALS, AND EXPRESSLY DISCLAIMS ALL IMPLIED WARRANTIES OF NONINFRINGEMENT,
// MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE.
//
// Information and code furnished is believed to be accurate and reliable.
// However, NVIDIA Corporation assumes no responsibility for the consequences of use of such
// information or for any infringement of patents or other rights of third parties that may
// result from its use. No license is granted by implication or otherwise under any patent
// or patent rights of NVIDIA Corporation. Details are subject to change without notice.
// This code supersedes and replaces all information previously supplied.
// NVIDIA Corporation products are not authorized for use as critical
// components in life support devices or systems without express written approval of
// NVIDIA Corporation.
//
// Copyright (c) 2008-2012 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_PHYSICS_NX_CLOTH
#define PX_PHYSICS_NX_CLOTH
/** \addtogroup cloth
@{
*/

#include "PxPhysX.h"
#include "PxActor.h"
#include "cloth/PxClothFabricTypes.h"
#include "cloth/PxClothTypes.h"
#include "cloth/PxClothCollisionData.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxScene;

/**
\brief Set of connected particles tailored towards simulating character cloth.
A cloth object consists of the following components:
\arg A set of particles that sample that cloth to simulate. The sampling does not need to be regular.
Particles are simulated in local space, which allows tuning the effect of changes to the global pose on the particles.
\arg Distance, bending, and shearing constraints between particles. 
These are stored in a PxClothFabric instance which can be shared across cloth instances.
\arg Spheres, capsules, planes, and convexes collision shapes. 
These shapes are all treated separately to the main PhysX rigid body scene.
\arg Virtual particles can be used to improve collision at a finer scale than the cloth sampling.
\arg Motion and separation constraints are used to limit the particle movement within or outside of a sphere.
*/
class PxCloth : public PxActor
{
public:
	virtual void release() = 0;

	/**
	\brief Returns a pointer to the corresponding cloth fabric.
	\return The associated cloth fabric.
	*/
	virtual	PxClothFabric* getFabric() const = 0;

	/**
	\brief Updates cloth particle location or inverse weight for current and previous particle state.
	\param [in] currentParticles The particle data for the current particle state or NULL if the state should not be changed.
	\param [in] previousParticles The particle data for the previous particle state or NULL if the state should not be changed.
	\note The invWeight stored in \a currentParticles is the new particle inverse mass, or zero for a static particle. 
	However, the invWeight stored in \a previousParticles is still used once for the next particle integration and fabric solve.
	\note If <b>currentParticles</b> or <b>previousParticles</b> are non-NULL then they must be the length specified by getNbParticles().
	\note This can be used to teleport particles (use same positions for current and previous).
	\see PxClothParticle
	*/
	virtual void setParticles(const PxClothParticle* currentParticles, const PxClothParticle* previousParticles) = 0;
	/**
	\brief Returns the number of particles.
	\return Number of particles.
	*/
	virtual PxU32 getNbParticles() const = 0;

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
	\param [out] motionConstraintBuffer Destination buffer, must be at least getNbMotionConstraints().
	\return True if the copy was successful.
	*/
	virtual bool getMotionConstraints(PxClothParticleMotionConstraint* motionConstraintBuffer) const = 0;
	/**
	\brief Returns the number of motion constraints.
	\return Number of motion constraints (same as getNbPartices() if enabled, 0 otherwise).
	*/
	virtual PxU32 getNbMotionConstraints() const = 0; 
	/**
	\brief Specifies motion constraint scale and bias.
	\param [in] scale Global scale multiplied to the radius of every motion constraint sphere (default = 1.0).
	\param [in] bias Global bias added to the radius of every motion constraint sphere (default = 0.0).
	*/
	virtual void setMotionConstraintScaleBias(PxReal scale, PxReal bias) = 0;
	/**
	\brief Reads back scale and bias factor for motion constraints.
	\see setMotionConstraintScaleBias()
	*/
	virtual void getMotionConstraintScaleBias(PxReal& scale, PxReal& bias) const = 0;

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
	\param [out] separationConstraintBuffer Destination buffer, must be at least getNbSeparationConstraints().
	\return True if the copy was successful.
	*/
	virtual bool getSeparationConstraints(PxClothParticleSeparationConstraint* separationConstraintBuffer) const = 0;
	/**
	\brief Returns the number of separation constraints.
	\return Number of separation constraints (same as getNbPartices() if enabled, 0 otherwise).
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
	\return Number of particle accelerations (same as getNbPartices() if enabled, 0 otherwise).
	*/
	virtual PxU32 getNbParticleAccelerations() const = 0; 

	/**
	\brief Updates location and radii of collision spheres.
	\param [in] sphereBuffer New sphere positions and radii by the end of the next simulate() call.
	\note A maximum of 32 spheres are supported.
	\see clearInterpolation()
	*/
	virtual void setCollisionSpheres(const PxClothCollisionSphere* sphereBuffer) = 0;
	/**
	\brief Retrieves the collision shapes. 
	\details Returns collision spheres, capsules, convexes, and triangles that were added through 
	the addCollision*() methods and modified through the setCollision*() methods.
	\param [out] sphereBuffer Spheres destination buffer, must be the same length as getNbCollisionSpheres().
	\param [out] pairIndexBuffer Capsules destination buffer, must be the same length as 2*getNbCollisionSpherePairs().
	\param [out] planesBuffer Planes destination buffer, must be the same length as getNbCollisionPlanes().
	\param [out] convexMaskBuffer Convexes destination buffer, must be the same length as getNbCollisionConvexes().
	\note Returns the positions at the end of the next simulate() call as specified by the setCollision*() methods.
	*/
	virtual void getCollisionData(PxClothCollisionSphere* sphereBuffer, PxU32* pairIndexBuffer,
		PxClothCollisionPlane* planesBuffer, PxU32* convexMaskBuffer) const = 0;
	/**
	\brief Returns the number of collision spheres.
	\return Number of collision spheres.
	*/
	virtual PxU32 getNbCollisionSpheres() const = 0;
	/**
	\brief Returns the number of collision capsules.
	\return Number of collision capsules.
	*/
	virtual PxU32 getNbCollisionSpherePairs() const = 0;

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
	\see clearInterpolation()
	*/
	virtual void setCollisionPlanes(const PxClothCollisionPlane* planesBuffer) = 0;
	/**
	\brief Adds a new collision convex.
	\details A collision convex is defined as the intersection of planes.
	\param [in] mask The bitmask of the planes that make up the convex.
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
	\brief Returns the number of collision planes.
	\return Number of collision planes.
	*/
	virtual PxU32 getNbCollisionPlanes() const = 0;
	/**
	\brief Returns the number of collision convexes.
	\return Number of collision convexes.
	*/
	virtual PxU32 getNbCollisionConvexes() const = 0;

	/**
	\brief Assigns virtual particles.
	\details Virtual particles provide more robust and accurate collision handling against collision spheres and capsules.
	More virtual particles will generally increase the accuracy of collision handling, and thus
	a sufficient number of virtual particles can mimic triangle-based collision handling.\n
	Virtual particles are specified as barycentric interpolation of real particles:
	The position of a virtual particle is w0 * P0 + w1 * P1 + w2 * P2, where P1, P2, P3 real particle positions.
	The barycentric weights w0, w1, w2 are stored in a separate table so they can be shared across multiple virtual particles.
	\param [in] numParticles total number of virtual particles.
	\param [in] triangleVertexAndWeightIndices Each virtual particle has four indices, the first three for real particle indices, and the last
	for the weight table index.  Thus, the length of <b>indices</b> needs to be 4*numVirtualParticles.
	\param [in] weightTableSize total number of unique weights triples.
	\param [in] triangleVertexWeightTable array for barycentric weights.
	\note Virtual particles only incur a runtime cost during the collision stage. Still, it is advisable to 
	only use virtual particles for areas where high collision accuracy is desired. (e.g. sleeve around very thin wrist).
	*/
	virtual void setVirtualParticles(PxU32 numParticles, const PxU32* triangleVertexAndWeightIndices, PxU32 weightTableSize, const PxVec3* triangleVertexWeightTable) = 0;
	/**
	\brief Returns the number of virtual particles.
	\return Number of virtual particles.
	*/
	virtual PxU32 getNbVirtualParticles() const = 0;
	/**
	\brief Copies index array of virtual particles to the user provided buffer.
	\param [out] userTriangleVertexAndWeightIndices Destination buffer, must be at least 4*getNbVirtualParticles().
	\see setVirtualParticles()
	*/
	virtual void getVirtualParticles(PxU32* userTriangleVertexAndWeightIndices) const = 0;
	/**
	\brief Returns the number of the virtual particle weights.
	\return Number of virtual particle weights.
	*/
	virtual PxU32 getNbVirtualParticleWeights() const = 0;
	/**
	\brief Copies weight table of virtual particles to the user provided buffer.
	\param [out] userTriangleVertexWeightTable Destination buffer, must be at least getNbVirtualParticleWeights().
	*/
	virtual void getVirtualParticleWeights(PxVec3* userTriangleVertexWeightTable) const = 0;

	/**
	\brief Sets global pose.
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
	\brief Sets target pose. 
	\details This function will move the cloth in world space. The resulting
	simulation may reflect inertia effect as a result of pose acceleration.
	\param [in] pose Target pose at the end of the next simulate() call.
	\see setGlobalPose() to move cloth without inertia effect.
	*/
	virtual void setTargetPose(const PxTransform& pose) = 0;

	/**
	\brief Sets the acceleration scale factor to adjust inertia effect from global pose changes.
	\param [in] scale New scale factor between 0.0 (no inertia) and 1.0 (full inertia) (default: 1.0).
	\note A value of 0.0 disables all inertia effects of accelerations applied through setTargetPos().
	\see setTargetPose() 
	*/
	virtual void setInertiaScale(PxReal scale) = 0;
	/**
	\brief Returns acceleration scale parameter.
	\return Acceleration scale parameter.
	*/
	virtual PxReal getInertiaScale() const = 0;
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
	\brief Sets the damping coefficient.
	\details The damping coefficient is the portion of local particle velocity that is canceled in 1/10 sec.
	\param [in] dampingCoefficient New damping coefficient between 0.0 and 1.0 (default: 0.0).
	*/
	virtual void setDampingCoefficient(PxReal dampingCoefficient) = 0;
	/**
	\brief Returns the damping coefficient.
	\return Damping coefficient.
	*/
	virtual PxReal getDampingCoefficient() const = 0;

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
	\brief Sets the drag coefficient.
	\details The drag coefficient is the portion of the pose velocity that is applied to each particle in 1/10 sec.
	\param [in] dragCoefficient New drag coefficient between 0.0f and 1.0 (default: 0.0).
	\note The drag coefficient shouldn't be set higher than the damping coefficient.
	*/
	virtual void setDragCoefficient(PxReal dragCoefficient) = 0;
	/**
	\brief Returns the drag coefficient.
	\return Drag coefficient.
	*/
	virtual PxReal getDragCoefficient() const = 0;

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

	/**
	\brief Sets the solver frequency parameter.
	\details Solver frequency specifies how often the simulation step is computed per second. 
	For example, a value of 60 represents one simulation step per frame
	in a 60fps scene.  A value of 120 will represent two simulation steps per frame, etc.
	\param [in] frequency Solver frequency per second.
	*/
	virtual void setSolverFrequency(PxReal frequency) = 0;
	/**
	\brief Returns solver frequency.
	\return Solver frequency.
	*/
	virtual PxReal getSolverFrequency() const = 0;

	/**
	\brief Sets solver configuration per phase type.
	\details Users can assign different solver configuration (solver type, stiffness, etc.) per each phase type (see PxClothFabricPhaseType).
	\param [in] phaseType Which phases to change the configuration for.
	\param [in] config What configuration to change it to.
	*/
	virtual void setPhaseSolverConfig(PxClothFabricPhaseType::Enum phaseType, const PxClothPhaseSolverConfig& config) = 0;
	/**
	\brief Reads solver configuration for specified phase type.
	\param [in] phaseType Which phase to return the configuration for.
	\return solver configuration (see PxClothPhaseSolverConfig)
	\note If <b> phaseType </b> is invalid, the returned solver configuration's solverType member will become eINVALID.
	*/
	virtual PxClothPhaseSolverConfig getPhaseSolverConfig(PxClothFabricPhaseType::Enum phaseType) const = 0;

	/**
	\brief Sets cloth flags (e.g. use GPU or not, use CCD or not).
	\param [in] flag Mask of which flags to set.
	\param [in] value Value to set flags to.
	*/
	virtual	void setClothFlag(PxClothFlag::Enum flag, bool value)	= 0;
	/**
	\brief Returns cloth flags.
	\return Cloth flags.
	*/
	virtual PxClothFlags getClothFlags() const = 0;

	/**
	\brief Returns true if cloth is in sleep state
	\return True if cloth is in sleep state.
	*/
	virtual bool isSleeping() const = 0;
	/**
	\brief Returns the velocity threshold for putting cloth in sleep state.
	\return Velocity threshold for putting cloth in sleep state.
	*/
	virtual PxReal getSleepLinearVelocity() const = 0;
	/**
	\brief Sets the velocity threshold for putting cloth in sleep state.
	\details If none of the particles moves faster (in local space)
	than the threshold for a while, the cloth will be put in
	sleep state and simulation will be skipped.
	\param [in] threshold Velocity threshold (default: 0.0f)
	*/
	virtual void setSleepLinearVelocity(PxReal threshold) = 0;
	/**
	\brief Forces cloth to wake up from sleep state.
	\details The wakeCounterValue determines how long all particles need to 
	move less than the velocity threshold until the cloth is put to sleep.
	\param[in] wakeCounterValue New wake counter value (range: [0, inf]).
	*/
	virtual void wakeUp(PxReal wakeCounterValue = PX_SLEEP_INTERVAL) = 0;
	/**
	\brief Forces cloth to be put in sleep state.
	*/
	virtual void putToSleep() = 0;

	/**
	\brief Locks the cloth solver so that external applications can safely read back particle data.
	\return See PxClothReadData for available user data for read back.
	*/
	virtual PxClothReadData* lockClothReadData() const = 0;

	/**
	\brief Returns previous time step size.
	\details Time between sampling of previous and current particle positions for computing particle velocity.
	\return Previous time step size.
	*/
	virtual PxReal getPreviousTimeStep() const = 0;

	/**
	\brief Returns world space bounding box.
	\return Particle bounds in global coordinates.
	*/
	virtual PxBounds3 getWorldBounds() const = 0;

	virtual		const char*					getConcreteTypeName() const					{	return "PxCloth"; }

protected:
	PxCloth(PxRefResolver& v) : PxActor(v)		{}
	PX_INLINE PxCloth() : PxActor() {}
	virtual ~PxCloth() {}
	virtual		bool						isKindOf(const char* name)	const		{	return !strcmp("PxCloth", name) || PxActor::isKindOf(name);		}
};

PX_INLINE PxCloth*			PxActor::isCloth()				{ return is<PxCloth>();			}
PX_INLINE const PxCloth*	PxActor::isCloth()		const	{ return is<PxCloth>();			}


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
