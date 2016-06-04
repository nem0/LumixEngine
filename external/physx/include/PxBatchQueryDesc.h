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


#ifndef PX_PHYSICS_NX_SCENEQUERYDESC
#define PX_PHYSICS_NX_SCENEQUERYDESC
/** \addtogroup physics 
@{ */

#include "PxPhysXConfig.h"
#include "PxClient.h"
#include "PxFiltering.h"
#include "PxQueryFiltering.h"
#include "PxQueryReport.h"

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
		\brief The query results were incomplete due to touch hit buffer overflow. Blocking hit is still correct.
		*/
		eOVERFLOW
	};
};

/**
\brief Generic struct for receiving results of single query in a batch. Gets templated on hit type PxRaycastHit, PxSweepHit or PxOverlapHit.
*/
template<typename HitType>
struct PxBatchQueryResult
{
	HitType			block;			//!< Holds the closest blocking hit for a single query in a batch. Only valid if hasBlock is true.
	HitType*		touches;		//!< This pointer will either be set to NULL for 0 nbTouches or will point
									//!< into the user provided batch query results buffer specified in PxBatchQueryDesc.
	PxU32			nbTouches;		//!< Number of touching hits returned by this query, works in tandem with touches pointer.
	void*			userData;		//!< Copy of the userData pointer specified in the corresponding query.
	PxU8			queryStatus;	//!< Takes on values from PxBatchQueryStatus::Enum.
	bool			hasBlock;		//!< True if there was a blocking hit.
	PxU16			pad;			//!< pads the struct to 16 bytes.

	/** \brief Computes the number of any hits in this result, blocking or touching. */
	PX_INLINE PxU32				getNbAnyHits() const				{ return nbTouches + (hasBlock ? 1 : 0); }

	/** \brief Convenience iterator used to access any hits in this result, blocking or touching. */
	PX_INLINE const HitType&	getAnyHit(const PxU32 index) const	{ PX_ASSERT(index < nbTouches + (hasBlock ? 1 : 0));
																		return index < nbTouches ? touches[index] : block; }
};

/** \brief Convenience typedef for the result of a batched raycast query. */
typedef PxBatchQueryResult<PxRaycastHit>	PxRaycastQueryResult;

/** \brief Convenience typedef for the result of a batched sweep query. */
typedef PxBatchQueryResult<PxSweepHit>		PxSweepQueryResult;

/** \brief Convenience typedef for the result of a batched overlap query. */
typedef PxBatchQueryResult<PxOverlapHit>	PxOverlapQueryResult;

/**
\brief Struct for #PxBatchQuery memory pointers.
 
@see PxBatchQuery PxBatchQueryDesc
*/
struct PxBatchQueryMemory
 {
 	/**
	\brief The pointer to the user-allocated buffer for results of raycast queries in corresponding order of issue
 
 	\note The size should be large enough to fit the number of expected raycast queries.
 	\note For ps3, this must be 16 bytes aligned and not on stack
 
 	@see PxRaycastQueryResult 
 	*/
 	PxRaycastQueryResult*			userRaycastResultBuffer;
 
 	/**
 	\brief The pointer to the user-allocated buffer for raycast touch hits.
 	\note The size of this buffer should be large enough to store PxRaycastHit. 
 	If the buffer is too small to store hits, the related PxRaycastQueryResult.queryStatus will be set to eOVERFLOW
 	\note For ps3, this buffer must be 16 bytes aligned and not on stack
 
 	*/
 	PxRaycastHit*					userRaycastTouchBuffer;
 
 	/**
 	\brief The pointer to the user-allocated buffer for results of sweep queries in corresponding order of issue
 
 	\note The size should be large enough to fit the number of expected sweep queries.
 	\note For ps3, this must be 16 bytes aligned and not on stack
 
 	@see PxRaycastQueryResult 
 	*/
 	PxSweepQueryResult*				userSweepResultBuffer;
 
 	/**
 	\brief The pointer to the user-allocated buffer for sweep hits.
 	\note The size of this buffer should be large enough to store PxSweepHit. 
 	If the buffer is too small to store hits, the related PxSweepQueryResult.queryStatus will be set to eOVERFLOW
 	\note For ps3, this buffer must be 16 bytes aligned and not on stack
 
 	*/
 	PxSweepHit*						userSweepTouchBuffer;
 
 	/**
 	\brief The pointer to the user-allocated buffer for results of overlap queries in corresponding order of issue
 
 	\note The size should be large enough to fit the number of expected overlap queries.
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
 	PxOverlapHit*					userOverlapTouchBuffer;
 
 	/** \brief Capacity of the user-allocated userRaycastTouchBuffer in elements */
 	PxU32							raycastTouchBufferSize;
 
 	/** \brief Capacity of the user-allocated userSweepTouchBuffer in elements */
 	PxU32							sweepTouchBufferSize;
 
 	/** \brief Capacity of the user-allocated userOverlapTouchBuffer in elements */
 	PxU32							overlapTouchBufferSize;

 	/** \return Capacity of the user-allocated userRaycastResultBuffer in elements (max number of raycast() calls before execute() call) */
	PX_FORCE_INLINE	PxU32			getMaxRaycastsPerExecute() const	{ return raycastResultBufferSize; }

 	/** \return Capacity of the user-allocated userSweepResultBuffer in elements (max number of sweep() calls before execute() call) */
	PX_FORCE_INLINE	PxU32			getMaxSweepsPerExecute() const		{ return sweepResultBufferSize; }

 	/** \return Capacity of the user-allocated userOverlapResultBuffer in elements (max number of overlap() calls before execute() call) */
	PX_FORCE_INLINE	PxU32			getMaxOverlapsPerExecute() const	{ return overlapResultBufferSize; }

	PxBatchQueryMemory(PxU32 raycastResultBufferSize_, PxU32 sweepResultBufferSize_, PxU32 overlapResultBufferSize_) :
		userRaycastResultBuffer	(NULL),
		userRaycastTouchBuffer	(NULL),
		userSweepResultBuffer	(NULL),
		userSweepTouchBuffer	(NULL),
		userOverlapResultBuffer	(NULL),
		userOverlapTouchBuffer	(NULL),
		raycastTouchBufferSize	(0),
		sweepTouchBufferSize	(0),
		overlapTouchBufferSize	(0),
		raycastResultBufferSize	(raycastResultBufferSize_),
		sweepResultBufferSize	(sweepResultBufferSize_),
		overlapResultBufferSize	(overlapResultBufferSize_)
	{
	}

protected:
 	PxU32							raycastResultBufferSize;
 	PxU32							sweepResultBufferSize;
 	PxU32							overlapResultBufferSize;
};

/**
\brief Maximum allowed size for combined SPU shader code and data size.
*/
#define PX_QUERY_SPU_SHADER_LIMIT 2048

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

	<b>Platform specific:</b> Applies to PS3 only.

	*/
	void*							spuPreFilterShader;

	/**
	\brief Size (in bytes) of the spu pre filter shader codes #spuPreFilterShader

	<b>Default:</b> 0

	<b>Platform specific:</b> Applies to PS3 only.

	\note spuPreFilterShaderSize+spuPostFilterShaderSize+filterShaderDataSize should <= PX_QUERY_SPU_SHADER_LIMIT

	@see spuPreFilterShader
	*/
	PxU32							spuPreFilterShaderSize;

	/**
	\brief The custom spu post filter shader to use for collision filtering.

	\note This parameter is a fragment of SPU binary codes with the similar function of #PxBatchQueryPostFilterShader.
	The requirement of the spu function is the same as PxBatchQueryPreFilterShader::filter. To compile the shader for 
	spu, you can reference the implementation, PreBuild configuration in the project file of SampleVehicle.If you don't want to define 
	your own filter shader you can just leave this variable as NULL.
	library.

	<b>Platform specific:</b> Applies to PS3 only.

	*/
	void*							spuPostFilterShader;

	/**
	\brief Size (in bytes) of the spu post filter shader codes #spuPostFilterShader

	<b>Default:</b> 0

	<b>Platform specific:</b> Applies to PS3 only.

	\note spuPreFilterShaderSize+spuPostFilterShaderSize+filterShaderDataSize should <= PX_QUERY_SPU_SHADER_LIMIT

	@see spuPostFilterShader
	*/
	PxU32							spuPostFilterShaderSize;

	/**
	\brief client that creates and owns this scene query.

	This value will be used as an override when PX_DEFAULT_CLIENT value is passed to the query in PxQueryFilterData.clientId.

	@see PxScene::createClient()
	*/
	PxClientID						ownerClient;

	/**
	\brief User memory buffers for the query.

	@see PxBatchQueryMemory
	*/
	PxBatchQueryMemory				queryMemory;	

	/**
	\brief PS3 only. Enables or disables SPU execution for this batch.

	Defaults to true on PS3, ignored on other platforms.
	*/
	bool							runOnSpu;

	/**
	\brief Construct a batch query with specified maximum number of queries per batch.

	If the number of raycasts/sweeps/overlaps per execute exceeds the limit, the query will be discarded with a warning.

	\param maxRaycastsPerExecute	Maximum number of raycast() calls allowed before execute() call.
									This has to match the amount of memory allocated for PxBatchQueryMemory::userRaycastResultBuffer.
	\param maxSweepsPerExecute	Maximum number of sweep() calls allowed before execute() call.
									This has to match the amount of memory allocated for PxBatchQueryMemory::userSweepResultBuffer.
	\param maxOverlapsPerExecute	Maximum number of overlap() calls allowed before execute() call.
									This has to match the amount of memory allocated for PxBatchQueryMemory::userOverlapResultBuffer.
	*/
	PX_INLINE						PxBatchQueryDesc(PxU32 maxRaycastsPerExecute, PxU32 maxSweepsPerExecute, PxU32 maxOverlapsPerExecute);
	PX_INLINE bool					isValid() const;
};


PX_INLINE PxBatchQueryDesc::PxBatchQueryDesc(PxU32 maxRaycastsPerExecute, PxU32 maxSweepsPerExecute, PxU32 maxOverlapsPerExecute) :
	filterShaderData		(NULL),
	filterShaderDataSize	(0),
	preFilterShader			(NULL),
	postFilterShader		(NULL),
	spuPreFilterShader		(NULL),
	spuPreFilterShaderSize	(0),
	spuPostFilterShader		(NULL),
	spuPostFilterShaderSize	(0),
	ownerClient				(PX_DEFAULT_CLIENT),
	queryMemory				(maxRaycastsPerExecute, maxSweepsPerExecute, maxOverlapsPerExecute),
	runOnSpu				(true)
{
}


PX_INLINE bool PxBatchQueryDesc::isValid() const
{ 
	if ( ((filterShaderDataSize == 0) && (filterShaderData != NULL)) ||
		 ((filterShaderDataSize > 0) && (filterShaderData == NULL)) )
		 return false;

#if defined(PX_PS3)

	if ( ((spuPreFilterShaderSize == 0)  && (spuPreFilterShader != NULL))  ||
		 ((spuPreFilterShaderSize > 0)   && (spuPreFilterShader == NULL))  ||
		 ((spuPostFilterShaderSize == 0) && (spuPostFilterShader != NULL)) ||
		 ((spuPostFilterShaderSize > 0)  && (spuPostFilterShader == NULL)) )
		 return false;

	if ( ((spuPreFilterShader != NULL)  && (preFilterShader == NULL)) ||
		 ((spuPostFilterShader != NULL) && (postFilterShader == NULL)))
		 return false;

	if ( ((spuPostFilterShaderSize + spuPreFilterShaderSize)  > 0)  &&
		 ((filterShaderDataSize + spuPostFilterShaderSize + spuPreFilterShaderSize) > PX_QUERY_SPU_SHADER_LIMIT) )
		 return false;

#endif

	return true;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
