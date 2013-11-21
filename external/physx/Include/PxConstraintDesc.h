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


#ifndef PX_PHYSICS_NX_CONSTRAINTDESC
#define PX_PHYSICS_NX_CONSTRAINTDESC

/** \addtogroup physics
@{
*/

#include "PxPhysX.h"
#include "foundation/PxFlags.h"
#include "foundation/PxMath.h"
#include "foundation/PxVec3.h"
#include "common/PxSerialFramework.h"

namespace physx { namespace debugger { namespace comm {
	class PvdDataStream;
}}}

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxConstraintConnector;
class PxRigidActor;
class PxScene;
class PxConstraintConnector;
class PxRenderBuffer;

/**
\brief constraint flags

\note eBROKEN is a read only flag
*/

struct PxConstraintFlag
{
	enum Type
	{
		eBROKEN					= 1<<0,			//< whether the constraint is broken
		ePROJECTION				= 1<<1,			//< whether projection is enabled for this constraint
		eCOLLISION_ENABLED		= 1<<2,			//< whether contacts should be generated between the objects this constraint constrains
		eREPORTING				= 1<<3,			//< whether this constraint should generate force reports
		eVISUALIZATION			= 1<<4,			//< whether this constraint should be visualized, if constraint visualization is turned on
	};
};

/**
\brief constraint flags
@see PxConstraintFlag
*/

typedef PxFlags<PxConstraintFlag::Type, PxU16> PxConstraintFlags;
PX_FLAGS_OPERATORS(PxConstraintFlag::Type, PxU16);


/**
 \brief constraint row flags

 These flags configure the post-processing of constraint rows and the behavior of the solver while solving constraints
*/

struct Px1DConstraintFlag
{
	enum Type
	{
		eSPRING					= 1<<0,		//< whether the constraint is a spring
		eACCELERATION_SPRING	= 1<<1,		//< whether the constraint is a force or acceleration spring
		eRESTITUTION			= 1<<2,		//< whether the restitution model should be applied to generate the target velocity
		eKEEPBIAS				= 1<<3,		//< for hard constraints, whether to keep the error term when solving the error-free velocity equation
		eOUTPUT_FORCE			= 1<<4		//< whether to accumulate the force value from this constraint for the reported constraint force.
	};
};

typedef PxFlags<Px1DConstraintFlag::Type, PxU16> Px1DConstraintFlags;
PX_FLAGS_OPERATORS(Px1DConstraintFlag::Type, PxU16);

/**
\brief A constraint

A constraint is expressed as a set of 1-dimensional constraint rows which define the required constraint
on the objects' velocities. 

Given these definitions, the solver attempts to generate 

1. a set of velocities for the objects which, when integrated, respect the constraint errors:
body0vel.dot(lin0,ang0) - body1vel.dot(lin1, ang1) + (geometricError / timestep) = velocityTarget

2. a set of velocities for the objects which respect the constraints:
body0vel.dot(lin0,ang0) - body1vel.dot(lin1, ang1) = velocityTarget

Alternatively, the solver can attempt to resolve the velocity constraint as an implicit spring:

F = spring * -geometricError + damping * (velocityTarget - body0vel.dot(lin0,ang0) + body1vel.dot(lin1, ang1))

where F is the constraint force, or as an acceleration spring where acceleration replaces force.
*/

PX_ALIGN_PREFIX(16)
struct Px1DConstraint
{
	PxVec3				linear0;				//< linear component of velocity jacobian in world space
	PxReal				geometricError;			//< geometric error of the constraint along this axis
	PxVec3				angular0;				//< angular component of velocity jacobian in world space
	PxReal				velocityTarget;			//< velocity target for the constraint along this axis

	PxVec3				linear1;				//< linear component of velocity jacobian in world space
	PxReal				minImpulse;				//< minimum impulse the solver may apply to enforce this constraint
	PxVec3				angular1;				//< angular component of velocity jacobian in world space
	PxReal				maxImpulse;				//< maximum impulse the solver may apply to enforce this constraint

	PxReal				spring;					//< spring parameter, for spring constraints
	PxReal				damping;				//< damping parameter, for spring constraints

	PxReal				restitution;			//< restitution parameter for determining additional "bounce"

	Px1DConstraintFlags	flags;					//< a set of Px1DConstraintFlags
	PxU16				solveGroup;				//< constraint optimization hint: make this 256 for hard constraints with unbounded force limits, 257 for hard unilateral constraints with [0, inf) force limits, and 0 otherwise
} 
PX_ALIGN_SUFFIX(16);


/** \brief flags for determining which components of the constraint should be visualized 
 */

struct PxConstraintVisualizationFlag
{
	enum Enum
	{
		eLOCAL_FRAMES	= 1,	//< visualize constraint frames
		eLIMITS			= 2		//< visualize constraint limits
	};
};


/** solver constraint generation shader

This function is called by the constraint solver framework. The function must be reentrant, since it may be called simultaneously
from multiple threads, and should access only the arguments passed into it, since on PS3 this function may execute on SPU. 

Developers writing custom constraints are encouraged to read the implementation code in PhysXExtensions.

\param[out] constraints an array of solver constraint rows to be filled in
\param[out] body0WorldOffset the origin point at which the constraint is resolved. This value does not affect how constraints are solved, but the 
force and torque reported for the constraint are resolved at this point
\param[in] constantBlock the constant data block
\param[in] maxConstraints the size of the constraint buffer. At most this many constraints rows may be written
\param[in] bodyAToWorld The world transform of the first constrained body (the identity if the body is NULL)
\param[in] bodyBToWorld The world transform of the second constrained body (the identity if the body is NULL)

\return the number of constraint rows written.
*/

typedef PxU32 (*PxConstraintSolverPrep)(Px1DConstraint* constraints,
										PxVec3& body0WorldOffset,
										PxU32 maxConstraints,
										const void* constantBlock,
										const PxTransform& bodyAToWorld,
										const PxTransform& bodyBToWorld);

/** solver constraint projection shader

This function is called by the constraint post-solver framework. The function must be reentrant, since it may be called simultaneously
from multiple threads and should access only the arguments passed into it, since on PS3 this function may execute on SPU.

\param[in] constantBlock the constant data block
\param[out] bodyAToWorld The world transform of the first constrained body (the identity if the body is NULL)
\param[out] bodyBToWorld The world transform of the second constrained body (the identity if the body is NULL)
\param[in] true if the constraint should be projected by moving the second body towards the first, false if the converse
*/

typedef void (*PxConstraintProject)(const void* constantBlock,
									PxTransform& bodyAToWorld,
									PxTransform& bodyBToWorld,
									bool projectToA);

/**
	API used to visualize details about a constraint.
*/
class PxConstraintVisualizer
{
protected:
	virtual ~PxConstraintVisualizer(){}
public:
	virtual void visualizeJointFrames( const PxTransform& parent, const PxTransform& child ) = 0;

	virtual void visualizeLinearLimit( const PxTransform& t0, const PxTransform& t1, PxReal value, bool active ) = 0;

	virtual void visualizeAngularLimit( const PxTransform& t0, PxReal lower, PxReal upper, bool active) = 0;

	virtual void visualizeLimitCone( const PxTransform& t, PxReal ySwing, PxReal zSwing, bool active) = 0;

	virtual void visualizeDoubleCone( const PxTransform& t, PxReal angle, bool active) = 0;
};

/** solver constraint visualization function

This function is called by the constraint post-solver framework to visualize the constraint

\param[out] out the render buffer to render to
\param[in] constantBlock the constant data block
\param[in] body0Transform The world transform of the first constrained body (the identity if the body is NULL)
\param[in] body1Transform The world transform of the second constrained body (the identity if the body is NULL)
\param[in] frameScale the visualization scale for the constraint frames
\param[in] limitScale the visualization scale for the constraint limits
\param[in] flags the visualization flags

@see PxRenderBuffer 
*/
typedef void (*PxConstraintVisualize)( PxConstraintVisualizer& visualizer,
									  const void* constantBlock,
									  const PxTransform& body0Transform,
									  const PxTransform& body1Transform,
									  PxU32 flags );


struct PxPvdUpdateType
{
	enum Enum
	{
		CREATE_INSTANCE,
		RELEASE_INSTANCE,
		UPDATE_ALL_PROPERTIES,
		UPDATE_SIM_PROPERTIES,
	};
};

/** 

\brief This class connects a custom constraint to the SDK

This class connects a custom constraint to the SDK, and functions are called by the SDK
to query the custom implementation for specific information to pass on to the application
or inform the constraint when the application makes calls into the SDK which will update
the custom constraint's internal implementation
*/

class PxConstraintConnector
{
public:
	/** 
	when the constraint is marked dirty, this function is called at the start of the simulation
	step for the SDK to copy the constraint data block.
	*/
	
	virtual void*			prepareData()													= 0;

	/** 
	this function is called by the SDK to update PVD's view of it
	*/
	
	virtual bool			updatePvdProperties(physx::debugger::comm::PvdDataStream& pvdConnection,
												const PxConstraint* c,
												PxPvdUpdateType::Enum updateType) const		= 0;

	/** 
	When the SDK deletes a PxConstraint object this function is called by the SDK. In general
	custom constraints should not be deleted directly by applications: rather, the constraint
	should respond to a release() request by calling PxConstraint::release(), then wait for
	this call to release its own resources, so that even if the release() call occurs during
	a simulation step, the deletion of the constraint is buffered until that step completes.
	
	This function is also called when a PxConstraint object is deleted on cleanup due to 
	destruction of the PxPhysics object.
	*/
	
	virtual void			onConstraintRelease()											= 0;

	/** 
	This function is called by the SDK when the CoM of one of the actors is moved. Since the
	API specifies constraint positions relative to actors, and the constraint shader functions
	are supplied with coordinates relative to bodies, some synchronization is usually required
	when the application moves an object's center of mass.
	*/

	virtual void			onComShift(PxU32 actor)											= 0;

	/**
	\brief Fetches external data for a constraint.
	
	This function is used by the SDK to acquire a reference to the owner of a constraint and a unique
	owner type ID. This information will be passed on when a breakable constraint breaks or when
	#PxConstraint::getExternalReference() is called.

	\param[out] typeID Unique type identifier of the external object. The value 0xffffffff is reserved and should not be used. Furthermore, if the PhysX extensions library is used, some other IDs are reserved already (see PxConstraintExtIDs)
	\return Reference to the external object which owns the constraint.

	@see PxConstraintInfo PxSimulationEventCallback.onConstraintBreak()
	*/
	virtual void*			getExternalReference(PxU32& typeID)								= 0;

	/**
	\brief virtual destructor
	*/
	virtual ~PxConstraintConnector() {};
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
