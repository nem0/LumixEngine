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
#ifndef PX_JOINT_REPX_EXTENSIONS_H
#define PX_JOINT_REPX_EXTENSIONS_H
/** \addtogroup extensions
  @{
*/

#include "RepX/RepX.h"
#include "foundation/PxSimpleTypes.h"
#include "extensions/PxD6Joint.h"
#include "extensions/PxDistanceJoint.h"
#include "extensions/PxFixedJoint.h"
#include "extensions/PxPrismaticJoint.h"
#include "extensions/PxRevoluteJoint.h"
#include "extensions/PxSphericalJoint.h"
#include "foundation/PxAssert.h"
#include "foundation/PxString.h"
#include "PxConstraintExt.h"
#include "PxConstraint.h"

namespace physx { namespace repx {

	class RepXCollection;


	//Id auto-generating function.
	PX_INLINE const void* getId( const PxJoint* inJoint )		{ return inJoint; }

	//Mapping from datatype to repx extension name
	PX_INLINE const char* getExtensionNameForType( const PxD6Joint* )		{ return "PxD6Joint"; }
	PX_INLINE const char* getExtensionNameForType( const PxDistanceJoint* )	{ return "PxDistanceJoint"; }
	PX_INLINE const char* getExtensionNameForType( const PxFixedJoint* )	{ return "PxFixedJoint"; }
	PX_INLINE const char* getExtensionNameForType( const PxPrismaticJoint* ){ return "PxPrismaticJoint"; }
	PX_INLINE const char* getExtensionNameForType( const PxRevoluteJoint* )	{ return "PxRevoluteJoint"; }
	PX_INLINE const char* getExtensionNameForType( const PxSphericalJoint* ){ return "PxSphericalJoint"; }
	PX_INLINE const char* getExtensionNameForType( const PxJoint* inJoint )
	{
		if ( inJoint )
		{
			switch( inJoint->getType() )
			{
			case PxJointType::eD6: return getExtensionNameForType( static_cast<const PxD6Joint*>( inJoint ) );
			case PxJointType::eDISTANCE: return getExtensionNameForType( static_cast<const PxDistanceJoint*>( inJoint ) );
			case PxJointType::eFIXED: return getExtensionNameForType( static_cast<const PxFixedJoint*>( inJoint ) );
			case PxJointType::ePRISMATIC: return getExtensionNameForType( static_cast<const PxPrismaticJoint*>( inJoint ) );
			case PxJointType::eREVOLUTE: return getExtensionNameForType( static_cast<const PxRevoluteJoint*>( inJoint ) );
			case PxJointType::eSPHERICAL: return getExtensionNameForType( static_cast<const PxSphericalJoint*>( inJoint ) );
			default:
				break;
			}
		}
		return "__unknown joint type__";
	}
	
	//Mapping from abstract pointer to specific point so that 
	//when converted to/from a void* we know exactly what we
	//are getting.
	PX_INLINE const void* getBasePtr( const PxJoint* inJoint )
	{
		if ( inJoint )
		{
			switch( inJoint->getType() )
			{
			case PxJointType::eD6: return static_cast<const PxD6Joint*>( inJoint );
			case PxJointType::eDISTANCE: return static_cast<const PxDistanceJoint*>( inJoint );
			case PxJointType::eFIXED: return static_cast<const PxFixedJoint*>( inJoint );
			case PxJointType::ePRISMATIC: return static_cast<const PxPrismaticJoint*>( inJoint );
			case PxJointType::eREVOLUTE: return static_cast<const PxRevoluteJoint*>( inJoint );
			case PxJointType::eSPHERICAL: return static_cast<const PxSphericalJoint*>( inJoint );
			default:
				break;
			}
		}
		return NULL;
	}
	
	//Operator on the actual data underlying the generic type.
	template<typename TResultType, typename TOperator>
	PX_INLINE TResultType visitJointRepXObject( const TRepXId inId, void* inLiveObject, const char* inRepXExtensionName, TOperator inOperator )
	{
		if ( PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxD6Joint*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxD6Joint*>( inLiveObject ) );
		else if ( PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxDistanceJoint*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxDistanceJoint*>( inLiveObject ) );
		else if ( PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxFixedJoint*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxFixedJoint*>( inLiveObject ) );
		else if ( PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxPrismaticJoint*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxPrismaticJoint*>( inLiveObject ) );
		else if ( PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxRevoluteJoint*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxRevoluteJoint*>( inLiveObject ) );
		else if ( PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxSphericalJoint*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxSphericalJoint*>( inLiveObject ) );
		else
			return inOperator( inId, inLiveObject, inRepXExtensionName );
	}
	

	PX_INLINE RepXAddToCollectionResult addToRepXCollection( RepXCollection& inCollection, RepXIdToRepXObjectMap& inIdMap, const PxConstraint& inType )
	{
		PxU32 constraintType = 0;
		void* joint = const_cast<PxConstraint*>( &inType )->getExternalReference( constraintType );
		if ( constraintType == PxConstraintExtIDs::eJOINT )
		{
			PxJoint* theJoint = reinterpret_cast<PxJoint*>( joint );
			return inCollection.addRepXObjectToCollection( RepXObject( getExtensionNameForType( theJoint ), joint, static_cast<TRepXId>(reinterpret_cast<size_t>(joint)) ), inIdMap );
		}
		else //RepX doesn't support non-joint constraints.
		{
			return RepXAddToCollectionResult::InvalidParameters;
		}
	}
	
	PxU32 getNumJointExtensions();
	//The repx collection is responsible for freeing these extensions.
	PxU32 createJointExtensions( RepXExtension** outExtensions, PxU32 outBufferSize, PxAllocatorCallback& inCallback );

}}

/** @} */
#endif
