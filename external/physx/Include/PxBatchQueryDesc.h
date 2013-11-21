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


#ifndef PX_PHYSICS_NX_SCENEQUERYDESC
#define PX_PHYSICS_NX_SCENEQUERYDESC
/** \addtogroup physics 
@{ */

#include "PxPhysX.h"
#include "PxClient.h"
#include "PxFiltering.h"
#include "PxSceneQueryFiltering.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

struct	PxSweepHit;
struct	PxRaycastHit;

struct PxBatchQueryStatus
{
	enum Enum
	{
		/**
		\brief This is the initial state before a query starts.
		*/
		ePENDING = 0,

		/**
		\brief The query is finished; results have been written into the result and hit buffers.
		*/
		eSUCCESS,

		/**
		\brief The query was aborted due to the hit buffer being full.
		*/
		eABORTED
	};
};

/**
\brief Struct for the result of a batched raycast query.
*/
struct PxRaycastQueryResult
{
	PxRaycastHit*	hits;
	PxU32			nbHits;
	PxU32			queryStatus;
	void*			userData;
};

/**
\brief Struct for the result of a batched sweep query.
*/
struct PxSweepQueryResult
{
	PxSweepHit*		hits;
	PxU32			nbHits;
	PxU32			queryStatus;
	void*			userData;
};

/**
\brief Struct for the result of a batched overlap query.
*/
struct PxOverlapQueryResult
{
	PxShape**		hits;
	PxU32			nbHits;
	PxU32			queryStatus;
	void*			userData;
};


/**
\brief Descriptor class for #PxBatchQuery.

@see PxBatchQuery PxSceneQueryExecuteMode
*/
class PxBatchQueryDesc
{
public:

	/**
	\brief Shared global filter data which will get passed into the filter shader.

	\note The provided data will get copied to internal buffers and this copy will be used for filtering calls.

	<b>Default:</b> NULL

	@see PxSimulationFilterShader
	*/
	void*							filterShaderData;

	/**
	\brief Size (in bytes) of the shared global filter data #filterShaderData.

	<b>Default:</b> 0

	@see PxSimulationFilterShader filterShaderData
	*/
	PxU32							filterShaderDataSize;

	/**
	\brief The custom preFilter shader to use for filtering.

	@see PxBatchQueryPreFilterShader PxDefaultPreFilterShader
	*/
	PxBatchQueryPreFilterShader		preFilterShader;	

		/**
	\brief The custom postFilter shader to use for filtering.

	@see PxBatchQueryPostFilterShader PxDefaultPostFilterShader
	*/
	PxBatchQueryPostFilterShader	postFilterShader;	

	/**
	\brief The custom spu pre filter shader to use for collision filtering.

	\note This parameter is a fragment of SPU binary codes with the similar function of #PxBatchQueryPreFilterShader. 
	The requirement of the spu function is the same as PxBatchQueryPreFilterShader::filter. To compile the shader for 
	spu, you can reference the implementation, makefile and awk scripts in SampleVehicle. If you don't want to define 
	your own filter shader you can just leave this variable as NULL.

	<b>Platform:</b>
	\li PC SW: Not applicable
	\li PS3  : Yes
	\li XB360: Not applicable
	\li WII	 : Not applicable

	*/
	void*							spuPreFilterShader;

		/**
	\brief Size (in bytes) of the spu pre filter shader codes #spuPreFilterShader

	<b>Default:</b> 0

	@see spuPreFilterShader
	*/
	PxU32							spuPreFilterShaderSize;

	/**
	\brief The custom spu post filter shader to use for collision filtering.

	\note This parameter is a fragment of SPU binary codes with the similar function of #PxBatchQueryPostFilterShader.
	The requirement of the spu function is the same as PxBatchQueryPreFilterShader::filter. To compile the shader for 
	spu, you can reference the implementation, makefile and awk scripts in SampleVehicle.If you don't want to define 
	your own filter shader you can just leave this variable as NULL.
	library.

	<b>Platform:</b>
	\li PC SW: Not applicable
	\li PS3  : Yes
	\li XB360: Not applicable
	\li WII	 : Not applicable

	*/
	void*							spuPostFilterShader;

		/**
	\brief Size (in bytes) of the spu post filter shader codes #spuPostFilterShader

	<b>Default:</b> 0

	@see spuPostFilterShader
	*/
	PxU32							spuPostFilterShaderSize;

	/**
	\brief Immutable client that creates and owns this scene query.

	NOT CURRENTLY SUPPORTED

	@see PxScene::createClient()
	*/
	PxClientID						ownerClient;

	/**
	\brief The pointer to the user-allocated buffer for raycast query result

	\note The length should be same as the number of raycast queries
	\note For ps3, this must be 16 bytes aligned and not on stack

	@see PxRaycastQueryResult 
	*/
	PxRaycastQueryResult*			userRaycastResultBuffer;

	/**
	\brief The pointer to the user-allocated buffer for raycast hits.
	\note The size of this buffer should be large enough to store PxRaycastHit. 
	If the buffer is too small to store hits, the related PxRaycastQueryResult.queryStatus will be set to eABORTED
	\note For ps3, this buffer must be 16 bytes aligned and not on stack

	*/
	PxRaycastHit*					userRaycastHitBuffer;

	/**
	\brief The pointer to the user-allocated buffer for sweep query result

	\note The length should be same as the number of sweep query
	\note For ps3, this must be 16 bytes aligned and not on stack

	@see PxRaycastQueryResult 
	*/
	PxSweepQueryResult*				userSweepResultBuffer;

	/**
	\brief The pointer to the user-allocated buffer for sweep hits.
	\note The size of this buffer should be large enough to store PxSweepHit. 
	If the buffer is too small to store hits, the related PxSweepQueryResult.queryStatus will be set to eABORTED
	\note For ps3, this buffer must be 16 bytes aligned and not on stack

	*/
	PxSweepHit*						userSweepHitBuffer;

	/**
	\brief The pointer to the user-allocated buffer for overlap query result

	\note The length should be same as the number of overlap query
	\note For ps3, this must be 16 bytes aligned and not on stack

	@see PxRaycastQueryResult 
	*/
	PxOverlapQueryResult*			userOverlapResultBuffer;

	/**
	\brief The pointer to the user-allocated buffer for overlap hits.
	\note The size of this buffer should be large enough to store the hits returned. 
	If the buffer is too small to store hits, the related PxOverlapQueryResult.queryStatus will be set to eABORTED
	\note For ps3, this buffer must be 16 bytes aligned and not on stack

	*/
	PxShape**						userOverlapHitBuffer;

	/**
	\brief The number of elements of the user-allocated userRaycastHitBuffer

	*/
	PxU32							raycastHitBufferSize;

	/**
	\brief The number of elements of the user-allocated userSweepHitBuffer

	*/
	PxU32							sweepHitBufferSize;

	/**
	\brief The number of elements of the user-allocated userOverlapHitBuffer

	*/
	PxU32							overlapHitBufferSize;
	

	PX_INLINE						PxBatchQueryDesc();
	PX_INLINE void					setToDefault();
	PX_INLINE bool					isValid() const;
};


PX_INLINE PxBatchQueryDesc::PxBatchQueryDesc() :
	filterShaderData		(NULL),
	filterShaderDataSize	(0),
	preFilterShader			(NULL),
	postFilterShader		(NULL),
	spuPreFilterShader		(NULL),
	spuPreFilterShaderSize	(0),
	spuPostFilterShader		(NULL),
	spuPostFilterShaderSize	(0),
	ownerClient				(PX_DEFAULT_CLIENT),
	userRaycastResultBuffer	(NULL),
	userRaycastHitBuffer	(NULL),
	userSweepResultBuffer	(NULL),
	userSweepHitBuffer		(NULL),
	userOverlapResultBuffer	(NULL),
	userOverlapHitBuffer	(NULL),
	raycastHitBufferSize	(0),
	sweepHitBufferSize		(0),
	overlapHitBufferSize	(0)
{
}


PX_INLINE void PxBatchQueryDesc::setToDefault()			
{ 
	*this = PxBatchQueryDesc();
}

PX_INLINE bool PxBatchQueryDesc::isValid() const
{ 
	if ( ((filterShaderDataSize == 0) && (filterShaderData != NULL)) ||
		 ((filterShaderDataSize > 0) && (filterShaderData == NULL)) )
		return false;

	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
