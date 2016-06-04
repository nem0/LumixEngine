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


#ifndef PX_FOUNDATION_PX_FOUNDATION_H
#define PX_FOUNDATION_PX_FOUNDATION_H

/** \addtogroup foundation
  @{
*/

#include "foundation/PxVersionNumber.h"
#include "foundation/PxErrors.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxErrorCallback;
class PxAllocatorCallback;
class PxBroadcastingAllocator;

/**
\brief Foundation SDK singleton class.

You need to have an instance of this class to instance the higher level SDKs.
*/
class PX_FOUNDATION_API PxFoundation
{
public:
	/**
	\brief Destroys the instance it is called on.

	The operation will fail, if there are still modules referencing the foundation object. Release all dependent modules prior
	to calling this method.

	@see PxCreateFoundation()
	*/
	virtual	void release() = 0;

	/**
	retrieves error callback
	*/
	virtual PxErrorCallback& getErrorCallback() const = 0;

	/**
	Sets mask of errors to report.
	*/
	virtual void setErrorLevel(PxErrorCode::Enum mask = PxErrorCode::eMASK_ALL) = 0;

	/**
	Retrieves mask of errors to be reported.
	*/
	virtual PxErrorCode::Enum getErrorLevel() const = 0;

	/**
	retrieves the current allocator.
	*/
	virtual PxBroadcastingAllocator& getAllocator() const = 0;
	
	/**
	Retrieves the allocator this object was created with.
	*/
	virtual PxAllocatorCallback& getAllocatorCallback() const = 0;

	/**
	Retrieves if allocation names are being passed to allocator callback.
	*/
	virtual bool getReportAllocationNames() const = 0;

	/**
	Set if allocation names are being passed to allocator callback.
	\details Enabled by default in debug and checked build, disabled by default in profile and release build.
	*/
	virtual void setReportAllocationNames(bool value) = 0;

protected:
	virtual ~PxFoundation() {}
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif


/**
\brief Creates an instance of the foundation class

The foundation class is needed to initialize higher level SDKs. There may be only one instance per process.
Calling this method after an instance has been created already will result in an error message and NULL will be returned.

\param version Version number we are expecting (should be PX_PHYSICS_VERSION)
\param allocator User supplied interface for allocating memory(see #PxAllocatorCallback)
\param errorCallback User supplied interface for reporting errors and displaying messages(see #PxErrorCallback)
\return Foundation instance on success, NULL if operation failed

@see PxFoundation
*/

PX_C_EXPORT PX_FOUNDATION_API physx::PxFoundation* PX_CALL_CONV PxCreateFoundation(physx::PxU32 version,
																			     physx::PxAllocatorCallback& allocator,
																			     physx::PxErrorCallback& errorCallback);
/**
\brief Retrieves the Foundation SDK after it has been created.

\note The behavior of this method is undefined if the foundation instance has not been created already.

@see PxCreateFoundation()
*/
PX_C_EXPORT PX_FOUNDATION_API physx::PxFoundation& PX_CALL_CONV PxGetFoundation();

 /** @} */
#endif // PX_FOUNDATION_PX_FOUNDATION_H
