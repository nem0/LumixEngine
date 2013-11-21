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


#ifndef PX_SIMULATION_STATISTICS
#define PX_SIMULATION_STATISTICS
/** \addtogroup physics
@{
*/

#include "PxPhysX.h"
#include "geometry/PxGeometry.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Class used to retrieve statistics for a simulation step.

<b>Platform:</b>
\li PC SW: Yes
\li PS3  : Yes
\li XB360: Yes
\li WII	 : Yes

@see PxScene::getSimulationStatistics()
*/
class PxSimulationStatistics
{
public:
	/**
	\brief Identifies each type of broadphase volume.
	@see numBroadPhaseAdds numBroadPhaseRemoves
	*/
	enum VolumeType
	{
		/**
		\brief A volume belonging to a rigid body object
		@see PxRigidStatic PxRigidDynamic PxArticulationLink
		*/
		eRIGID_BODY,

		/**
		\brief A volume belonging to a particle system
		@see PxParticleSystem PxParticleFluid
		*/
		ePARTICLE_SYSTEM,

		eVOLUME_COUNT
	};

	/**
	\brief Different types of rigid body collision pair statistics.
	@see getRbPairStats
	*/
	enum RbPairStatsType
	{
		/**
		\brief Shape pairs processed as discrete contact pairs for the current simulation step.
		*/
		eDISCRETE_CONTACT_PAIRS,

		/**
		\brief Shape pairs processed as swept integration pairs for the current simulation step.

		\note Counts the pairs for which special CCD (continuous collision detection) work was actually done and NOT the number of pairs which were configured for CCD. 
		Furthermore, there can be multiple CCD passes and all processed pairs of all passes are summed up, hence the number can be larger than the amount of pairs which have been configured for CCD.

		@see PxPairFlag::eSWEPT_INTEGRATION_LINEAR
		*/
		eSWEPT_INTEGRATION_PAIRS,

		/**
		\brief Shape pairs processed with user contact modification enabled for the current simulation step.

		@see PxContactModifyCallback
		*/
		eMODIFIED_CONTACT_PAIRS,

		/**
		\brief Trigger shape pairs processed for the current simulation step.

		@see PxShapeFlag::eTRIGGER_SHAPE
		*/
		eTRIGGER_PAIRS
	};


//objects:
	/**
	\brief Number of active PxConstraint objects (joints etc.) for the current simulation step.
	*/
	PxU32   numActiveConstraints;

	/**
	\brief Number of active dynamic bodies for the current simulation step.

	\note Does not include active kinematic bodies
	*/
	PxU32   numActiveDynamicBodies;

	/**
	\brief Number of active kinematic bodies for the current simulation step.
	*/
	PxU32   numActiveKinematicBodies;

	/**
	\brief Number of static bodies for the current simulation step.
	*/
	PxU32	numStaticBodies;

	/**
	\brief Number of dynamic bodies for the current simulation step.

	\note Includes inactive and kinematic bodies, and articulation links
	*/
	PxU32   numDynamicBodies;

	/**
	\brief Number of shapes of each geometry type.
	*/

	PxU32	numShapes[PxGeometryType::eGEOMETRY_COUNT];

//solver:
	/**
	\brief The number of 1D axis constraints(joints+contact) present in the current simulation step.
	*/
	PxU32	numAxisSolverConstraints;

//broadphase:
	/**
	\brief Get number of broadphase volumes of a certain type added for the current simulation step.

	\param[in] type The volume type for which to get the number
	\return Number of broadphase volumes added.

	@see VolumType
	*/
	PxU32 getNumBroadPhaseAdds(VolumeType type) const
	{
		if (type != eVOLUME_COUNT)
			return numBroadPhaseAdds[type];
		else
		{
			PX_ASSERT(false);
			return 0;
		}
	}

	/**
	\brief Get number of broadphase volumes of a certain type removed for the current simulation step.

	\param[in] type The volume type for which to get the number
	\return Number of broadphase volumes removed.

	@see VolumType
	*/
	PxU32 getNumBroadPhaseRemoves(VolumeType type) const
	{
		if (type != eVOLUME_COUNT)
			return numBroadPhaseRemoves[type];
		else
		{
			PX_ASSERT(false);
			return 0;
		}
	}

//collisions:
	/**
	\brief Get number of shape collision pairs of a certain type processed for the current simulation step.

	There is an entry for each geometry pair type.

	\note entry[i][j] = entry[j][i], hence, if you want the sum of all pair
	      types, you need to discard the symmetric entries

	\param[in] pairType The type of pair for which to get information
	\param[in] g0 The geometry type of one pair object
	\param[in] g1 The geometry type of the other pair object
	\return Number of processed pairs of the specified geometry types.
	*/
	PxU32 getRbPairStats(RbPairStatsType pairType, PxGeometryType::Enum g0, PxGeometryType::Enum g1) const
	{
		if (g0 >= PxGeometryType::eGEOMETRY_COUNT || g1 >= PxGeometryType::eGEOMETRY_COUNT)
		{
			PX_ASSERT(false);
			return 0;
		}

		switch(pairType)
		{
			case eDISCRETE_CONTACT_PAIRS:
				return numDiscreteContactPairs[g0][g1];

			case eSWEPT_INTEGRATION_PAIRS:
				return numSweptIntegrationPairs[g0][g1];

			case eMODIFIED_CONTACT_PAIRS:
				return numModifiedContactPairs[g0][g1];

			case eTRIGGER_PAIRS:
				return numTriggerPairs[g0][g1];

			default:
				PX_ASSERT(false);
				return 0;
		}
	}

	PxSimulationStatistics()
	{
		for(PxU32 i=0; i < eVOLUME_COUNT; i++)
		{
			numBroadPhaseAdds[i] = 0;
			numBroadPhaseRemoves[i] = 0;
		}

		for(PxU32 i=0; i < PxGeometryType::eGEOMETRY_COUNT; i++)
		{
			for(PxU32 j=0; j < PxGeometryType::eGEOMETRY_COUNT; j++)
			{
				numDiscreteContactPairs[i][j] = 0;
				numModifiedContactPairs[i][j] = 0;
				numSweptIntegrationPairs[i][j] = 0;
				numTriggerPairs[i][j] = 0;
			}
		}

		for(PxU32 i=0; i < PxGeometryType::eGEOMETRY_COUNT; i++)
		{
			numShapes[i] = 0;
		}

		numActiveConstraints = 0;
		numActiveDynamicBodies = 0;
		numActiveKinematicBodies = 0;
		numStaticBodies = 0;
		numDynamicBodies = 0;

		numAxisSolverConstraints = 0;
	}


	//
	// We advise to not access these members directly. Use the provided accessor methods instead.
	//
//broadphase:
	PxU32	numBroadPhaseAdds[eVOLUME_COUNT];
	PxU32	numBroadPhaseRemoves[eVOLUME_COUNT];

//collisions:
	PxU32   numDiscreteContactPairs[PxGeometryType::eGEOMETRY_COUNT][PxGeometryType::eGEOMETRY_COUNT];
	PxU32   numSweptIntegrationPairs[PxGeometryType::eGEOMETRY_COUNT][PxGeometryType::eGEOMETRY_COUNT];
	PxU32   numModifiedContactPairs[PxGeometryType::eGEOMETRY_COUNT][PxGeometryType::eGEOMETRY_COUNT];
	PxU32   numTriggerPairs[PxGeometryType::eGEOMETRY_COUNT][PxGeometryType::eGEOMETRY_COUNT];
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
