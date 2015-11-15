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


#ifndef PX_PHYSICS_DELAY_LOAD_HOOK
#define PX_PHYSICS_DELAY_LOAD_HOOK

#include <foundation/PxPreprocessor.h>
#include <common/PxPhysXCommonConfig.h>

/** \addtogroup foundation
@{
*/

#ifndef PX_DOXYGEN
namespace physx
{
#endif
	/**
 	\brief PxDelayLoadHook

	This is a helper class for delay loading the PhysXCommon dll. If a PhysXCommon dll with a different file name than default needs to be loaded, it is possible to assign this name by subclassing this class and implementing the virtual member functions to return the correct file names.

	Once the names are set, the instance must be set for use by PhysX.dll using setPhysXInstance() and by PhysXCooking .dll using setPhysXCookingInstance().

	@see PxSetPhysXDelayLoadHook(), PxSetPhysXCookingDelayLoadHook()
 	*/
	class PxDelayLoadHook
	{
	public:
		PxDelayLoadHook() {}
		virtual ~PxDelayLoadHook() {}

		virtual const char* getPhysXCommonDEBUGDllName() const = 0;
		virtual const char* getPhysXCommonCHECKEDDllName() const = 0;
		virtual const char* getPhysXCommonPROFILEDllName() const = 0;
		virtual const char* getPhysXCommonDllName() const = 0;

	protected:
	private:
	};

	/**
	\brief Sets delay load hook instance for PhysX dll.

	\param[in] hook Delay load hook.

	@see PxDelayLoadHook
	*/
	PX_C_EXPORT PX_PHYSX_CORE_API void PX_CALL_CONV PxSetPhysXDelayLoadHook(const physx::PxDelayLoadHook* hook);

	/**
	\brief Sets delay load hook instance for PhysXCooking dll.

	\param[in] hook Delay load hook.

	@see PxDelayLoadHook
	*/
	PX_C_EXPORT PX_PHYSX_CORE_API void PX_CALL_CONV PxSetPhysXCookingDelayLoadHook(const physx::PxDelayLoadHook* hook);


#ifndef PX_DOXYGEN
} // namespace physx
#endif
/** @} */
#endif
