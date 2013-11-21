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


#ifndef PX_COOKING_H
#define PX_COOKING_H
/** \addtogroup cooking
@{
*/
#include "common/PxPhysXCommon.h"      
#include "cooking/Pxc.h"

#include "cooking/PxConvexMeshDesc.h"
#include "cooking/PxTriangleMeshDesc.h"
#include "cooking/PxClothMeshDesc.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxOutputStream;
class PxBinaryConverter;

struct PxPlatform
{
	enum Enum
	{
		ePC,
		eXENON,
		ePLAYSTATION3,
		eWII,
		eARM,
	};
};

/**

\brief Structure describing parameters affecting mesh cooking.

@see PxSetCookingParams() PxGetCookingParams()
*/
struct PxCookingParams
{
	/**
	\brief Target platform

	Should be set to the platform which you intend to load the cooked mesh data on. This allows
	the SDK to optimize the mesh data in an appropriate way for the platform and make sure that
	endianness issues are accounted for correctly.

	<b>Default value:</b> Same as the platform on which the SDK is running.
	*/
	PxPlatform::Enum	targetPlatform;

	/**
	\brief Skin width for convexes

	Specifies the amount to inflate the convex mesh by when the new convex hull generator is used.

	Inflating the mesh allows the user to hide interpenetration errors by increasing the size of the
	collision mesh with respect to the size of the rendered geometry.

	<b>Default value:</b> 0.025f
	*/
	float		skinWidth;

	/**
	\brief When true, the face remap table is not created.  This saves a significant amount of memory, but the SDK will 
		not be able to provide information about which mesh triangle is hit in collisions, sweeps or raycasts hits.

	<b>Default value:</b> false
	*/
	bool		suppressTriangleMeshRemapTable;

	PxCookingParams():
		skinWidth(0.025f),
		suppressTriangleMeshRemapTable(false)
	{
#if defined(PX_X86) || defined(PX_X64)
		targetPlatform = PxPlatform::ePC;
#elif defined(PX_X360)
		targetPlatform = PxPlatform::eXENON;
#elif defined(PX_PS3)
		targetPlatform = PxPlatform::ePLAYSTATION3;
#elif defined(PX_WII)
		targetPlatform = PxPlatform::eWII;
#elif defined(PX_ARM)
		targetPlatform = PxPlatform::eARM;
#else
#error Unknown platform
#endif
	}	
};

class PxCooking
{
public:
	/**
	\brief Closes this instance of the interface.
	*/
	virtual void  release() = 0;

	/**
	\brief Sets cooking parameters

	\param[in] params Cooking parameters

	@see getParams()
	*/
	virtual void  setParams(const PxCookingParams& params) = 0;

	/**
	\brief Gets cooking parameters

	\return Current cooking parameters.

	@see PxCookingParams setParams()
	*/
	virtual const PxCookingParams& getParams() = 0;

	/**
	\brief Checks endianness is the same between cooking & target platforms

	\return True if there is and endian mismatch.
	*/
	virtual bool  platformMismatch() = 0;

	/**
	\brief Cooks a triangle mesh. The results are written to the stream.

	To create a triangle mesh object it is necessary to first 'cook' the mesh data into
	a form which allows the SDK to perform efficient collision detection.

	cookTriangleMesh() allows a mesh description to be cooked into a binary stream
	suitable for loading and performing collision detection at runtime.

	Example

	\include PxCookTriangleMesh_Example.cpp

	\param[in] desc The triangle mesh descriptor to read the mesh from.
	\param[in] stream User stream to output the cooked data.
	\return true on success

	@see cookConvexMesh() setParams()
	*/
	virtual bool  cookTriangleMesh(const PxTriangleMeshDesc& desc, PxOutputStream& stream) = 0;


	/**
	\brief Cooks a convex mesh. The results are written to the stream.

	To create a triangle mesh object it is necessary to first 'cook' the mesh data into
	a form which allows the SDK to perform efficient collision detection.

	cookConvexMesh() allows a mesh description to be cooked into a binary stream
	suitable for loading and performing collision detection at runtime.

	Example

	\include PxCookConvexMesh_Example.cpp

	\note This method is not reentrant if the convex mesh descriptor has the flag #PxConvexFlag::eCOMPUTE_CONVEX set.

	\param[in] desc The convex mesh descriptor to read the mesh from.
	\param[in] stream User stream to output the cooked data.
	\return true on success

	@see cookTriangleMesh() setParams()
	*/

	virtual bool  cookConvexMesh(const PxConvexMeshDesc& desc, PxOutputStream& stream) = 0;

	/**
	\brief Cooks a triangle mesh to a PxClothFabric.

	\param desc The cloth mesh descriptor on which the generation of the cooked mesh depends.
	\param gravityDir A normalized vector which specifies the direction of gravity. This information allows the cooker to generate a fabric
	with higher quality simulation behavior.
	\param stream The stream the cooked fabric is written to.
	\return True if cooking was successful
	*/

	virtual bool  cookClothFabric(const PxClothMeshDesc& desc, const PxVec3& gravityDir, PxOutputStream& stream) = 0;

	/**
	\brief Creates binary converter.

	\param[in] error		User-defined error callback. This is an optional parameter.

	\return Binary converter instance.
	*/
	virtual	PxBinaryConverter*	createBinaryConverter(physx::PxErrorCallback* error=NULL)	= 0;

protected:
	virtual ~PxCooking(){}
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/**
\brief Create an instance of the cooking interface.

Note that the foundation object is handled as an application-wide singleton in statically linked executables 
and a DLL-wide singleton in dynamically linked executables. Therefore, if you are using the runtime SDK in the 
same executable as cooking, you should pass the Physics's copy of foundation (acquired with 
PxPhysics::getFoundation()) to the cooker. This will also ensure correct handling of memory for objects
passed from the cooker to the SDK. 

To use cooking in standalone mode, create an instance of the Foundation object with PxCreateCookingFoundation.
You should pass the same foundation object to all instances of the cooking interface.

\param[in] version the SDK version number
\param[in] foundation the foundation object associated with this instance of the cooking interface.
\param[in] params the parameters for this instance of the cooking interface
\return true on success.

*/

PX_C_EXPORT PX_PHYSX_COOKING_API physx::PxCooking* PX_CALL_CONV PxCreateCooking(physx::PxU32 version,
																				physx::PxFoundation& foundation,
																				const physx::PxCookingParams& params);

/** @} */
#endif
