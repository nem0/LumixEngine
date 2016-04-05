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



#ifndef PX_REPX_SIMPLE_TYPE_H
#define PX_REPX_SIMPLE_TYPE_H

/** \addtogroup extensions
  @{
*/

#include "foundation/PxSimpleTypes.h"
#include "cooking/PxCooking.h"
#include "common/PxStringTable.h"
#include "common/PxSerialFramework.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif
	
	/**
	\brief Helper class containing the mapping of id to object, and type name.
	*/
	struct PxRepXObject
	{
		/**
		\brief Identifies the extension meant to handle this object.
		@see PxTypeInfo, PX_DEFINE_TYPEINFO, PxRepXSerializer
		*/
		const char*			typeName;

		/**
		\brief Pointer to the serializable this was created from
		*/
		const void*			serializable;

		/**
		\brief Id given to this object at some point
		*/
		PxSerialObjectId 	id;
		PxRepXObject( const char* inTypeName = "", const void* inSerializable = NULL, const PxSerialObjectId inId = 0 )
			: typeName( inTypeName )
			, serializable( inSerializable )
			, id( inId )
		{
		}
		bool isValid() const { return serializable != NULL; }
	};

	/**
	\brief Arguments required to instantiate a serializable object from RepX.

	Extra arguments can be added to the object map under special ids.

	@see PxRepXSerializer::objectToFile, PxRepXSerializer::fileToObject
	*/
	struct PxRepXInstantiationArgs
	{
		PxPhysics&			physics;
		PxCooking*			cooker;
		PxStringTable*		stringTable;
		PxRepXInstantiationArgs( PxPhysics& inPhysics, PxCooking* inCooking = NULL , PxStringTable* inStringTable = NULL ) 
			: physics( inPhysics )
			, cooker( inCooking )
			, stringTable( inStringTable )
		{
		}

		PxRepXInstantiationArgs& operator=(const PxRepXInstantiationArgs&);
	};


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif 