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

#ifndef PX_PHYSICS_EXTENSIONS_DEFAULT_ERROR_CALLBACK_H
#define PX_PHYSICS_EXTENSIONS_DEFAULT_ERROR_CALLBACK_H

#include "foundation/PxErrorCallback.h"
#include "PxPhysXConfig.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	/**
	\brief default implementation of the error callback

	This class is provided in order to enable the SDK to be started with the minimum of user code. Typically an application
	will use its own error callback, and log the error to file or otherwise make it visible. Warnings and error messages from
	the SDK are usually indicative that changes are required in order for PhysX to function correctly, and should not be ignored.
	*/

	class PxDefaultErrorCallback : public PxErrorCallback
	{
	public:
		PxDefaultErrorCallback();
		~PxDefaultErrorCallback();

		virtual void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line);
	};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#endif
