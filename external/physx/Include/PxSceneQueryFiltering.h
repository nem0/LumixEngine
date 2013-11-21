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


#ifndef PX_PHYSICS_NX_SCENE_QUERY_FILTERING
#define PX_PHYSICS_NX_SCENE_QUERY_FILTERING
/** \addtogroup scenequery
@{
*/

#include "PxPhysX.h"
#include "PxFiltering.h"
#include "foundation/PxFlags.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxShape;
class PxActor;
struct PxSceneQueryHit;


/**
\brief Filtering flags for scene queries.

@see PxSceneQueryFilter.filterFlags
*/
struct PxSceneQueryFilterFlag
{
	enum Enum
	{
		eSTATIC				= (1<<0),						//!< Traverse static shapes
		eDYNAMIC			= (1<<1),						//!< Traverse dynamic shapes
		ePREFILTER			= (1<<2),						//!< Run the pre-intersection-test filter (see #PxSceneQueryFilterCallback::preFilter())
		ePOSTFILTER			= (1<<3),						//!< Run the post-intersection-test filter (see #PxSceneQueryFilterCallback::postFilter())
		eMESH_MULTIPLE		= (1<<4),						//!< Generate all hits for meshes rather than just the first
		eBACKFACE			= (1<<5),						//!< Generate hits for exit points and back faces of tris - NOT CURRENTLY SUPPORTED
	};
};
PX_COMPILE_TIME_ASSERT(PxSceneQueryFilterFlag::eSTATIC==(1<<0));
PX_COMPILE_TIME_ASSERT(PxSceneQueryFilterFlag::eDYNAMIC==(1<<1));

/**
\brief Collection of set bits defined in PxSceneQueryFilterFlag.

@see PxSceneQueryFilter
*/
typedef PxFlags<PxSceneQueryFilterFlag::Enum,PxU16> PxSceneQueryFilterFlags;
PX_FLAGS_OPERATORS(PxSceneQueryFilterFlag::Enum,PxU16);

/**
\brief Classification of scene query hits.

A hit type of eNONE means that the hit should not be reported. 

In the case of raycastMultiple and sweepMultiple queries, hits of type eTOUCH will be returned which are closer than the 
first eBLOCK, together with the closest hit of type eBLOCK. For example, to return all hits in a raycastMultiple, always return eTOUCH.

For raycastSingle/sweepSingle, the closest hit of type eBLOCK is returned.

@see PxSceneQueryFilter.preFilter PxSceneQueryFilter.postFilter
*/
struct PxSceneQueryHitType
{
	enum Enum
	{
		eNONE	= 0,	//!< the query should ignore this shape
		eTOUCH	= 1,	//!< a hit on the shape touches the intersection geometry of the query but does not block it
		eBLOCK	= 2		//!< a hit on the shape blocks the query
	};
};

/**
\brief Scene query filtering data.

When the scene graph traversal determines that a shape intersects, filtering is performed.

Filtering is performed in the following order:

\li For non-batched queries only:<br>If the data field is non-zero, and the bitwise-AND value of data AND the shape's
queryFilterData is zero, the shape is skipped
\li If the filter callbacks are enabled in the flags field (see #PxSceneQueryFilterFlags) they will get invoked accordingly.
\li If neither #PxSceneQueryFilterFlag::ePREFILTER or #PxSceneQueryFilterFlag::ePOSTFILTER is set, the hit is 
assumed to be of type #PxSceneQueryHitType::eBLOCK for single hit queries and of type
#PxSceneQueryHitType::eTOUCH for multi hit queries.

@see PxScene.raycastAny PxScene.raycastSingle PxScene.raycastMultiple PxScene.sweepSingle PxScene.sweepMultiple
*/
struct PxSceneQueryFilterData
{
	/**
	\brief constructor sets to default 
	*/
	PX_INLINE PxSceneQueryFilterData() : flags(PxSceneQueryFilterFlag::eDYNAMIC | PxSceneQueryFilterFlag::eSTATIC) {}

	/**
	\brief constructor to set properties
	*/
	PX_INLINE PxSceneQueryFilterData(const PxFilterData& fd, PxSceneQueryFilterFlags f) : data(fd), flags(f) {}

	/**
	\brief constructor to set filter flags only
	*/
	PX_INLINE PxSceneQueryFilterData(PxSceneQueryFilterFlags f) : flags(f) {}

	PxFilterData data;				//!< Filter data associated with the scene query
	PxSceneQueryFilterFlags flags;	//!< Filter flags (see #PxSceneQueryFilterFlags)
};

/**
\brief Scene query filtering callbacks.

Custom filtering logic for scene query intersection candidates. If an intersection candidate object passes the data based filter (see #PxSceneQueryFilterData),
the filtering callbacks run on request (see #PxSceneQueryFilterData.flags)

\li If #PxSceneQueryFilterFlag::ePREFILTER is set, the preFilter function runs before precise intersection
testing. If this function returns #PxSceneQueryHitType::eTOUCH or #PxSceneQueryHitType::eBLOCK, precise testing is performed to 
determine intersection point(s).

The prefilter may overwrite the copy of filterFlags passed in the query's PxSceneQueryFilterData, in order to specify #PxSceneQueryFilterFlag::eBACKFACE and 
#PxSceneQueryFilterFlag::eMESH_MULTIPLE on a per-shape basis. Changes apply only to the shape being filtered, and changes to other flags are ignored.

\li If #PxSceneQueryFilterFlag::ePREFILTER is not set, precise intersection testing is performed with the 
#PxSceneQueryFilterFlag::eBACKFACE and #PxSceneQueryFilterFlag::eMESH_MULTIPLE flags from the filterFlags field.

\li If the #PxSceneQueryFilterFlag::ePOSTFILTER flag is set, the postFilter function is called for each intersection
point to determine touch/block status. This overrides any touch/block status returned from the preFilter function for this shape.

Filtering calls are not in order along the query direction, rather they are processed in the order in which
candidate shapes for testing are found by PhysX' scene traversal algorithms.

@see PxScene.raycastAny PxScene.raycastSingle PxScene.raycastMultiple PxScene.sweepSingle PxScene.sweepMultiple PxScene.overlapMultiple
*/
class PxSceneQueryFilterCallback
{
public:

	/**
	\brief Filter callback before precise intersection testing.

	\param[in] filterData The custom filter data of the query
	\param[in] shape The potentially hit shape
	\param[in,out] filterFlags The query filter flags from the query's PxSceneQueryFilterData (only the flags eMESH_MULTIPLE, eBACKFACE can be modified)

	@see PxSceneQueryFilterCallback
	*/
	virtual PxSceneQueryHitType::Enum preFilter(const PxFilterData& filterData, PxShape* shape, PxSceneQueryFilterFlags& filterFlags) = 0;

	/**
	\brief Filter callback after precise intersection testing.

	\param[in] filterData The custom filter data of the query
	\param[in] hit Scene query hit information. For overlap tests the faceIndex member is not valid. For sweepSingle/sweepMultiple and raycastSingle/raycastMultiple the hit information can be casted to #PxSweepHit and #PxRaycastHit respectively
	\return Hit declaration.

	@see PxSceneQueryFilterCallback
	*/
	virtual PxSceneQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxSceneQueryHit& hit) = 0;

	/**
	\brief virtual destructor
	*/	
	virtual ~PxSceneQueryFilterCallback() {}
};


/**
\brief Batched query prefiltering shader.

Custom filtering logic for batched query intersection candidates. If an intersection candidate object passes the data based filter (see #PxSceneQueryFilterData),
the filtering shader run on request (see #PxSceneQueryFilterData.flags)

\li If #PxSceneQueryFilterFlag::ePREFILTER is set, the #PxBatchQueryPreFilterShader runs before precise intersection
testing. If this function returns #PxSceneQueryHitType::eTOUCH or #PxSceneQueryHitType::eBLOCK, precise testing is performed to 
determine intersection point(s).

The #PxBatchQueryPreFilterShader may overwrite the copy of filterFlags passed in, in order to specify #PxSceneQueryFilterFlag::eBACKFACE and 
#PxSceneQueryFilterFlag::eMESH_MULTIPLE on a per-shap basis. Changes apply only to the shape being filtered, and changes to other flags are ignored.

\li If #PxSceneQueryFilterFlag::ePREFILTER is not set, precise intersection testing is performed with the 
#PxSceneQueryFilterFlag::eBACKFACE and #PxSceneQueryFilterFlag::eMESH_MULTIPLE flags from the filterFlags field.

Filtering shaders are not in order along the query direction, rather they are processed in the order in which
candidate shapes for testing are found by PhysX' scene traversal algorithms.

@see PxBatchQueryDesc.preFilterShader PxSceneQueryFilterCallback.preFilter PxBatchQueryPostFilterShader

*/

//	/**
//	\param[in] filterData0 The custom filter data of the query
//	\param[in] filterData1 The custom filter data of the second object
//	\param[in] constantBlock The constant global filter data (see #PxBatchQuery)
//	\param[in] constantBlockSize Size of the global filter data (see #PxBatchQuery)
//	\param[out] filterFlags Flags giving additional information on how an accepted pair should get processed
//	\return Hit declaration.
//
//	@see PxBatchQueryPostFilterShader 
//	*/
typedef PxSceneQueryHitType::Enum (*PxBatchQueryPreFilterShader)(	
	PxFilterData filterData0, 
	PxFilterData filterData1,
	const void* constantBlock, PxU32 constantBlockSize,
	PxSceneQueryFilterFlags& filterFlags);

/**
\brief Batched query postfiltering shader.

Custom filtering logic for batched query intersection candidates. If an intersection candidate object passes the data based filter (see #PxSceneQueryFilterData),
the filtering shader run on request (see #PxSceneQueryFilterData.flags)

\li If the #PxSceneQueryFilterFlag::ePOSTFILTER flag is set, the #PxBatchQueryPostFilterShader function is called for each intersection
point to determine touch/block status. This overrides any touch/block status returned from the #PxBatchQueryPreFilterShader for this shape.

Filtering shaders are not in order along the query direction, rather they are processed in the order in which
candidate shapes for testing are found by PhysX' scene traversal algorithms.

@see PxBatchQueryDesc.postFilterShader PxSceneQueryFilterCallback.postFilter PxBatchQueryPreFilterShader
*/

//	/**
//	\param[in] filterData0 The custom filter data of the query
//	\param[in] filterData1 The custom filter data of the shape
//	\param[in] constantBlock The constant global filter data (see #PxBatchQuery)
//	\param[in] constantBlockSize Size of the global filter data (see #PxBatchQuery)
//	\param[in] hit declaration.
//
//	@see PxBatchQueryPreFilterShader 
//	*/

typedef PxSceneQueryHitType::Enum (*PxBatchQueryPostFilterShader)(
	PxFilterData filterData0,
	PxFilterData filterData1,
	const void* constantBlock, PxU32 constantBlockSize,
	const PxSceneQueryHit& hit);

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
