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


#ifndef PX_PHYSICS_NX_CONSTRAINT
#define PX_PHYSICS_NX_CONSTRAINT

/** \addtogroup physics
@{
*/

#include "PxPhysX.h"
#include "PxConstraintDesc.h"
#include "common/PxSerialFramework.h"

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

struct PxConstraintShaderTable
{
	enum
	{
		eMAX_SOLVERPREPSPU_BYTESIZE=19056
	};

	enum
	{
		eMAX_SOLVERPRPEP_DATASIZE=364
	};

	PxConstraintSolverPrep			solverPrep;					//< solver constraint generation function
	void*							solverPrepSpu;				//< spu-optimized solver constraint generation function
	PxU32							solverPrepSpuByteSize;		//< code size of the spu-optimized solver constraint generation function
	PxConstraintProject				project;					//< constraint projection function
	PxConstraintVisualize			visualize;					//< constraint visualization function
};


/**
\brief a plugin class for implementing constraints

@see PxConstraint
*/

class PxConstraint : public PxSerializable
{
public:
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
	\brief Retrieve the constraint force most recently applied to maintain this constraint.
	
	\param[out] linear the constraint force
	\param[out] angular the constraint torque
	*/
	virtual void				getForce(PxVec3& linear, PxVec3& angular)				const	= 0;


	/**
	\brief Set the break force and torque thresholds for this constraint. 
	
	If either the force or torque measured at the constraint exceed these thresholds the constraint will break.

	\param[in] linear the linear break threshold
	\param[in] angular the angular break threshold
	*/

	virtual	void				setBreakForce(PxReal linear, PxReal angular)					= 0;

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
	virtual void*				getExternalReference(PxU32& typeID)								= 0;

	/**
	\brief Set the constraint functions for this constraint
	
	\param[in] connector the constraint connector object by which the SDK communicates with the constraint.
	\param[in] shaders the shader table for the constraint
 
	@see PxConstraintConnector PxConstraintSolverPrep PxConstraintProject PxConstraintVisualize
	*/
	virtual	void				setConstraintFunctions(PxConstraintConnector& connector,
													   const PxConstraintShaderTable& shaders)		= 0;

	virtual		const char*		getConcreteTypeName() const					{	return "PxConstraint"; }

protected:
								PxConstraint()											{}
								PxConstraint(PxRefResolver& v) :	PxSerializable(v)	{}
	virtual						~PxConstraint() {}
	virtual		bool			isKindOf(const char* name)	const		{	return !strcmp("PxConstraint", name) || PxSerializable::isKindOf(name);		}

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
