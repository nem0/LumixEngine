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


#ifndef PX_PHYSICS_NX_CONSTRAINT
#define PX_PHYSICS_NX_CONSTRAINT

/** \addtogroup physics
@{
*/

#include "PxPhysXConfig.h"
#include "PxConstraintDesc.h"
#include "common/PxBase.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxRigidActor;
class PxScene;
class PxConstraintConnector;

/**
\brief a table of function pointers for a constraint

@see PxConstraint
*/

/**
\brief constraint flags

\note eBROKEN is a read only flag
*/

struct PxConstraintFlag
{
	enum Enum
	{
		eBROKEN						= 1<<0,			//!< whether the constraint is broken
		ePROJECT_TO_ACTOR0			= 1<<1,			//!< whether actor1 should get projected to actor0 for this constraint (note: projection of a static/kinematic actor to a dynamic actor will be ignored)
		ePROJECT_TO_ACTOR1			= 1<<2,			//!< whether actor0 should get projected to actor1 for this constraint (note: projection of a static/kinematic actor to a dynamic actor will be ignored)
		ePROJECTION					= ePROJECT_TO_ACTOR0 | ePROJECT_TO_ACTOR1,	//!< whether the actors should get projected for this constraint (the direction will be chosen by PhysX)
		eCOLLISION_ENABLED			= 1<<3,			//!< whether contacts should be generated between the objects this constraint constrains
		eREPORTING					= 1<<4,			//!< whether this constraint should generate force reports. DEPRECATED, as constraints always generate reports
		eVISUALIZATION				= 1<<5,			//!< whether this constraint should be visualized, if constraint visualization is turned on
		eDRIVE_LIMITS_ARE_FORCES	= 1<<6,			//!< limits for drive strength are forces rather than impulses
		eDEPRECATED_32_COMPATIBILITY= 1<<7,			//!< legacy compatibility flag for 3.3; see user guide. This flag must not be set in order for drives to conform to an implicit spring model
		eIMPROVED_SLERP				= 1<<8			//!< perform preprocessing for improved accuracy on D6 Slerp Drive (this flag will be removed in a future release when preprocessing is no longer required)
	};
};

/**
\brief constraint flags
@see PxConstraintFlag
*/

typedef PxFlags<PxConstraintFlag::Enum, PxU16> PxConstraintFlags;
PX_FLAGS_OPERATORS(PxConstraintFlag::Enum, PxU16)


struct PxConstraintShaderTable
{
	enum
	{
		eMAX_SOLVERPREPSPU_BYTESIZE=19056
	};

	enum
	{
		eMAX_SOLVERPRPEP_DATASIZE=400
	};

	PxConstraintSolverPrep			solverPrep;					//< solver constraint generation function
	void*							solverPrepSpu;				//< spu-optimized solver constraint generation function
	PxU32							solverPrepSpuByteSize;		//< code size of the spu-optimized solver constraint generation function
	PxConstraintProject				project;					//< constraint projection function
	PxConstraintVisualize			visualize;					//< constraint visualization function
};


/**
\brief A plugin class for implementing constraints

@see PxPhysics.createConstraint
*/

class PxConstraint : public PxBase
{
public:

	/**
	\brief Releases a PxConstraint instance.

	\note This call does not wake up the connected rigid bodies.

	@see PxPhysics.createConstraint, PxBase.release()
	*/
	virtual void				release()														= 0;

	/**
	\brief Retrieves the scene which this constraint belongs to.

	\return Owner Scene. NULL if not part of a scene.

	@see PxScene
	*/
	virtual PxScene*			getScene()												const	= 0;

	/**
	\brief Retrieves the actors for this constraint.

	\param[out] actor0 a reference to the pointer for the first actor
	\param[out] actor1 a reference to the pointer for the second actor

	@see PxActor
	*/

	virtual void				getActors(PxRigidActor*& actor0, PxRigidActor*& actor1)	const	= 0;


	/**
	\brief Sets the actors for this constraint.

	\param[in] actor0 a reference to the pointer for the first actor
	\param[in] actor1 a reference to the pointer for the second actor

	@see PxActor
	*/

	virtual void				setActors(PxRigidActor* actor0, PxRigidActor* actor1)			= 0;

	/**
	\brief Notify the scene that the constraint shader data has been updated by the application
	*/

	virtual void				markDirty()														= 0;

	/**
	\brief Set the flags for this constraint

	\param[in] flags the new constraint flags

	default: PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES

	@see PxConstraintFlags
	*/

	virtual void				setFlags(PxConstraintFlags flags)								= 0;

	/**
	\brief Retrieve the flags for this constraint

	\return the constraint flags
	@see PxConstraintFlags
	*/

	virtual PxConstraintFlags	getFlags()												const	= 0;


	/**
	\brief Set a flag for this constraint

	\param[in] flag the constraint flag
	\param[in] value the new value of the flag

	@see PxConstraintFlags
	*/

	virtual void				setFlag(PxConstraintFlag::Enum flag, bool value)				= 0;

	/**
	\brief Retrieve the constraint force most recently applied to maintain this constraint.
	
	\param[out] linear the constraint force
	\param[out] angular the constraint torque
	*/
	virtual void				getForce(PxVec3& linear, PxVec3& angular)				const	= 0;


	/**
	\brief whether the constraint is valid. 
	
	A constraint is valid if it has at least one dynamic rigid body or articulation link. A constraint that
	is not valid may not be inserted into a scene, and therefore a static actor to which an invalid constraint
	is attached may not be inserted into a scene.

	Invalid constraints arise only when an actor to which the constraint is attached has been deleted.

	*/
	virtual bool				isValid() const													= 0;

	/**
	\brief Set the break force and torque thresholds for this constraint. 
	
	If either the force or torque measured at the constraint exceed these thresholds the constraint will break.

	\param[in] linear the linear break threshold
	\param[in] angular the angular break threshold
	*/


	virtual	void				setBreakForce(PxReal linear, PxReal angular)				= 0;

	/**
	\brief Retrieve the constraint break force and torque thresholds
	
	\param[out] linear the linear break threshold
	\param[out] angular the angular break threshold

	*/
	virtual	void				getBreakForce(PxReal& linear, PxReal& angular)		const	= 0;


	/**
	\brief Fetch external owner of the constraint.
	
	Provides a reference to the external owner of a constraint and a unique owner type ID.

	\param[out] typeID Unique type identifier of the external object.
	\return Reference to the external object which owns the constraint.

	@see PxConstraintConnector.getExternalReference()
	*/
	virtual void*				getExternalReference(PxU32& typeID)							= 0;

	/**
	\brief Set the constraint functions for this constraint
	
	\param[in] connector the constraint connector object by which the SDK communicates with the constraint.
	\param[in] shaders the shader table for the constraint
 
	@see PxConstraintConnector PxConstraintSolverPrep PxConstraintProject PxConstraintVisualize
	*/
	virtual	void				setConstraintFunctions(PxConstraintConnector& connector,
													   const PxConstraintShaderTable& shaders)		= 0;

	virtual	const char*			getConcreteTypeName() const { return "PxConstraint"; }

protected:
	PX_INLINE					PxConstraint(PxType concreteType, PxBaseFlags baseFlags) : PxBase(concreteType, baseFlags) {}
	PX_INLINE					PxConstraint(PxBaseFlags baseFlags) : PxBase(baseFlags) {}
	virtual						~PxConstraint() {}
	virtual	bool				isKindOf(const char* name) const { return !strcmp("PxConstraint", name) || PxBase::isKindOf(name); }

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
