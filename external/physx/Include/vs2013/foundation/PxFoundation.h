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
// Copyright (c) 2008-2014 NVIDIA Corporation. All rights reserved.
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
