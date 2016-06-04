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


#ifndef PX_FOUNDATION_PX_ERROR_CALLBACK_H
#define PX_FOUNDATION_PX_ERROR_CALLBACK_H

/** \addtogroup foundation
@{
*/

#include "foundation/PxErrors.h"
#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief User defined interface class.  Used by the library to emit debug information.

\note The SDK state should not be modified from within any error reporting functions.

<b>Threading:</b> The SDK sequences its calls to the output stream using a mutex, so the class need not
be implemented in a thread-safe manner if the SDK is the only client.
*/
class PxErrorCallback
{
public:

	virtual ~PxErrorCallback() {}

	/**
	\brief Reports an error code.
	\param code Error code, see #PxErrorCode
	\param message Message to display.
	\param file File error occured in.
	\param line Line number error occured on.
	*/
	virtual void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) = 0;

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
