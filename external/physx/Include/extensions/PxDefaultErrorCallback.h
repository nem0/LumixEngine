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
