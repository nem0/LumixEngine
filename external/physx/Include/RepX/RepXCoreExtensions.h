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
#ifndef REPXCOREEXTENSIONSH
#define REPXCOREEXTENSIONSH
#include "RepX/RepX.h"
#include "PxActor.h"
#include "PxArticulationLink.h"
#include "foundation/PxString.h"

namespace physx
{
	class PxActor;
	class PxAggregate;
	class PxTriangleMesh;
	class PxConvexMesh;
	class PxRigidDynamic;
	class PxRigidStatic;
	class PxMaterial;
	class PxArticulation;
	class PxArticulationLink;
	class PxCloth;
	class PxClothFabric;
	class PxParticleSystem;
	class PxParticleFluid;
}

namespace physx { namespace repx {

	PX_INLINE const void* getId( const PxActor* inActor )		{ return inActor; }
	PX_INLINE const void* getId( const PxTriangleMesh* inMesh ) { return inMesh; }
	PX_INLINE const void* getId( const PxConvexMesh* inMesh )	{ return inMesh; }
	PX_INLINE const void* getId( const PxHeightField* inMesh )	{ return inMesh; }
	PX_INLINE const void* getId( const PxMaterial* inData )		{ return inData; }
	PX_INLINE const void* getId( const PxArticulation* inData )	{ return inData; }
	PX_INLINE const void* getId( const PxCloth* inData )		{ return inData; }
	PX_INLINE const void* getId( const PxClothFabric* inData )	{ return inData; }
	PX_INLINE const void* getId( const PxParticleSystem* inData )	{ return inData; }
	PX_INLINE const void* getId( const PxParticleFluid* inData )	{ return inData; }
	PX_INLINE const void* getId( const PxAggregate* inData )	{ return inData; }

	PX_INLINE const char* getExtensionNameForType( const PxParticleFluid* )		{ return "PxParticleFluid"; }
	PX_INLINE const char* getExtensionNameForType( const PxParticleSystem* )	{ return "PxParticleSystem"; }
	PX_INLINE const char* getExtensionNameForType( const PxClothFabric* )		{ return "PxClothFabric"; }
	PX_INLINE const char* getExtensionNameForType( const PxCloth* )				{ return "PxCloth"; }
	PX_INLINE const char* getExtensionNameForType( const PxRigidDynamic* )		{ return "PxRigidDynamic"; }
	PX_INLINE const char* getExtensionNameForType( const PxRigidStatic* )		{ return "PxRigidStatic"; }
	PX_INLINE const char* getExtensionNameForType( const PxTriangleMesh* )		{ return "PxTriangleMesh"; }
	PX_INLINE const char* getExtensionNameForType( const PxConvexMesh* )		{ return "PxConvexMesh"; }
	PX_INLINE const char* getExtensionNameForType( const PxHeightField* )		{ return "PxHeightField"; }
	PX_INLINE const char* getExtensionNameForType( const PxMaterial* )			{ return "PxMaterial"; }
	PX_INLINE const char* getExtensionNameForType( const PxArticulation* )		{ return "PxArticulation"; }
	PX_INLINE const char* getExtensionNameForType( const PxAggregate* )			{ return "PxAggregate"; }
	PX_INLINE const char* getExtensionNameForType( const PxActor* inActor )	
	{
		if ( inActor )
		{
			if ( inActor->is<PxRigidDynamic>() )
				return getExtensionNameForType( inActor->is<PxRigidDynamic>() );
			else if ( inActor->is<PxRigidStatic>() )
				return getExtensionNameForType( inActor->is<PxRigidStatic>() );
		}
		return "";
	}

	
	PX_INLINE const void* getBasePtr( const PxRigidDynamic* inData )	{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxRigidStatic* inData )		{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxTriangleMesh* inData )	{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxConvexMesh* inData )		{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxHeightField* inData )		{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxMaterial* inData )		{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxArticulation* inData )	{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxClothFabric* inData )		{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxCloth* inData )			{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxParticleSystem* inData )	{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxParticleFluid* inData )	{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxAggregate* inData )		{ return  inData; }
	PX_INLINE const void* getBasePtr( const PxActor* inActor )	
	{ 
		if ( inActor )
		{
			if ( inActor->is<PxRigidDynamic>() )
				return inActor->is<PxRigidDynamic>();
			else if ( inActor->is<PxRigidStatic>() )
				return inActor->is<PxRigidStatic>();
			else if ( inActor->isArticulationLink() )
				return inActor->isArticulationLink();
		}
		return NULL;
	}
	
	template<typename TDataType>
	PX_INLINE RepXObject createRepXObject( const TDataType* inType, const TRepXId inId )
	{
		return RepXObject( getExtensionNameForType( inType ), getBasePtr( inType ), inId );
	}
	
	template<typename TDataType>
	PX_INLINE RepXObject createRepXObject( const TDataType* inType )
	{
		return createRepXObject( inType, static_cast<const TRepXId>( reinterpret_cast<const size_t>(getId( inType )) ) );
	}

	template<typename TDataType>
	PX_INLINE RepXAddToCollectionResult addToRepXCollection( RepXCollection& inCollection, RepXIdToRepXObjectMap& inIdMap, const TDataType& inType )
	{
		return inCollection.addRepXObjectToCollection( createRepXObject( &inType ), inIdMap );
	}
	
	/**
	 *	Add to a repx collection and with no-fail conditions, meaning assert on failure.
	 */
	template<typename TDataType>
	PX_INLINE void addToRepXCollectionNF( RepXCollection& inCollection, RepXIdToRepXObjectMap& inIdMap, const TDataType& inType )
	{
		RepXAddToCollectionResult theResult( addToRepXCollection( inCollection, inIdMap, inType ) );
		PX_ASSERT( RepXAddToCollectionResult::Success == theResult.mResult );
	}

	//Operator on the actual data underlying the generic type.
	template<typename TResultType, typename TOperator>
	PX_INLINE TResultType visitCoreRepXObject( const TRepXId inId, void* inLiveObject, const char* inRepXExtensionName, TOperator inOperator )
	{
		if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxRigidDynamic*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxRigidDynamic*>( inLiveObject ) );
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxRigidStatic*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxRigidStatic*>( inLiveObject ) );
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxTriangleMesh*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxTriangleMesh*>( inLiveObject ) );
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxConvexMesh*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxConvexMesh*>( inLiveObject ) );
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxHeightField*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxHeightField*>( inLiveObject ) );
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxMaterial*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxMaterial*>( inLiveObject ) );
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxArticulation*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxArticulation*>( inLiveObject ) );
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxAggregate*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxAggregate*>( inLiveObject ) );
#if PX_USE_CLOTH_API
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxCloth*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxCloth*>( inLiveObject ) );
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxClothFabric*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxClothFabric*>( inLiveObject ) );
#endif
#if PX_USE_PARTICLE_SYSTEM_API
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxParticleSystem*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxParticleSystem*>( inLiveObject ) );
		else if ( physx::PxStricmp( inRepXExtensionName, getExtensionNameForType( (PxParticleFluid*)NULL ) ) == 0 )
			return inOperator( inId, reinterpret_cast<PxParticleFluid*>( inLiveObject ) );
#endif
		else
			return inOperator( inId, inLiveObject, inRepXExtensionName );
	}

	template<typename TDataType> PX_INLINE const char* getItemName() { PX_ASSERT(false); return ""; }
	template<> PX_INLINE const char* getItemName<PxMaterial>() { return "PxMaterialRef"; }

	typedef RepXExtension* (*TAllocationFunction)( PxAllocatorCallback& inAllocator );
	struct ExtensionAllocator
	{
		TAllocationFunction mAllocationFunction;
		ExtensionAllocator( TAllocationFunction inFn )
			: mAllocationFunction( inFn )
		{
		}
		RepXExtension* allocateExtension(PxAllocatorCallback& inCallback) { return mAllocationFunction(inCallback); }
	};

	PxU32 getNumCoreExtensions();
	PxU32 createCoreExtensions( RepXExtension** outExtensions, PxU32 outBufferSize, PxAllocatorCallback& inCallback );

	//Definitions needed internally in the extension headers.
	template<typename TTriIndexElem>
	struct Triangle
	{
		TTriIndexElem mIdx0;
		TTriIndexElem mIdx1;
		TTriIndexElem mIdx2;
		Triangle( TTriIndexElem inIdx0 = 0, TTriIndexElem inIdx1 = 0, TTriIndexElem inIdx2 = 0)
			: mIdx0( inIdx0 )
			, mIdx1( inIdx1 )
			, mIdx2( inIdx2 )
		{
		}
	};

} }

#endif
