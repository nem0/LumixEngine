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
#ifndef PX_REPXUTILITY_H
#define PX_REPXUTILITY_H
#include "PxPhysics.h"
#include "extensions/PxJointRepXExtensions.h"
#include "RepXCoreExtensions.h"
#include "PxScene.h"
#include "PxRigidStatic.h"
#include "PxRigidDynamic.h"
#include "common/PxSerialFramework.h"
#include "geometry/PxConvexMesh.h"
#include "geometry/PxTriangleMesh.h"
#include "PxMaterial.h"
#include "geometry/PxHeightField.h"
#include "PxArticulation.h"
#include "RepXUpgrader/RepXUpgrader.h"
#include "cloth/PxClothFabric.h"
#include "cloth/PxCloth.h"
#include "foundation/PxAllocatorCallback.h"
#include "foundation/PxFoundation.h"

namespace physx { namespace repx {
	
/** @return Number of materials in the sdk */
inline PxU32 getObjectCount( PxMaterial*, PxPhysics& inPhysics ) { return inPhysics.getNbMaterials(); }
/** @return Number of convex meshes in the sdk */
inline PxU32 getObjectCount( PxConvexMesh*, PxPhysics& inPhysics ) { return inPhysics.getNbConvexMeshes(); }
/** @return Number of triangle meshes in the sdk */
inline PxU32 getObjectCount( PxTriangleMesh*, PxPhysics& inPhysics ) { return inPhysics.getNbTriangleMeshes(); }
/** @return Number of height fields in the sdk */
inline PxU32 getObjectCount( PxHeightField*, PxPhysics& inPhysics ) { return inPhysics.getNbHeightFields(); }
/** @return Number of rigid statics in the scene */
inline PxU32 getObjectCount( PxRigidStatic*, PxScene& inScene ) { return inScene.getNbActors(PxActorTypeSelectionFlag::eRIGID_STATIC); }
/** @return Number of rigid dynamics in the scene */
inline PxU32 getObjectCount( PxRigidDynamic*, PxScene& inScene ) { return inScene.getNbActors(PxActorTypeSelectionFlag::eRIGID_DYNAMIC); }
/** @return Number of articulations in the scene */
inline PxU32 getObjectCount( PxArticulation*, PxScene& inScene ) { return inScene.getNbArticulations(); }
#if PX_USE_CLOTH_API
/** @return Number of ClothFabrics in the sdk */
inline PxU32 getObjectCount( PxClothFabric*, PxPhysics& inPhysics ) { return inPhysics.getNbClothFabrics(); }
#endif

#if PX_USE_PARTICLE_SYSTEM_API
/** @return Number of particle systems in the scene */
inline PxU32 getObjectCount( PxParticleSystem*, PxScene& inScene ) { return inScene.getNbActors(PxActorTypeSelectionFlag::ePARTICLE_SYSTEM); }
/** @return Number of particle fluid in the scene */
inline PxU32 getObjectCount( PxParticleFluid*, PxScene& inScene ) { return inScene.getNbActors(PxActorTypeSelectionFlag::ePARTICLE_FLUID); }
#endif

inline PxU32 getJointObjectAndCount( PxConstraint** outConstraints, PxScene& inScene )
{
	PxU32 totalCount = inScene.getNbConstraints(); 
	if( totalCount == 0)
		return 0;
	
	PxAllocatorCallback& callback = PxGetFoundation().getAllocatorCallback();
	PxConstraint** theData = (PxConstraint**)callback.allocate(totalCount * sizeof( PxConstraint* ), "", __FILE__, __LINE__); 
	if( !theData )
		return 0;
	
	inScene.getConstraints( theData, totalCount );
	
	PxU32 constraintType = 0;
	PxU32 objCount = 0;
	for ( PxU32 idx = 0; idx < totalCount; ++idx )
	{	
		theData[idx]->getExternalReference( constraintType );
		if ( constraintType == PxConstraintExtIDs::eJOINT )
		{
			if (NULL != outConstraints)
 				outConstraints[objCount] = theData[idx];
			++objCount;
		}
	}		

	callback.deallocate( theData );

	return objCount;
}

/** @return Number of constraints in the scene. Ignore any constraints that are not joints. */
inline PxU32 getObjectCount( PxConstraint*, PxScene& inScene ) 
{
	return getJointObjectAndCount( NULL, inScene );
}

#if PX_USE_CLOTH_API
/** @return Number of cloth in the scene */
inline PxU32 getObjectCount( PxCloth*, PxScene& inScene  ) {  return inScene.getNbActors(PxActorTypeSelectionFlag::eCLOTH);  }
#endif

/** @return Number of aggregates in the scene */
inline PxU32 getObjectCount( PxAggregate*, PxScene& inScene  ) {  return inScene.getNbAggregates();  }

/** Fill an array with the materials in the SDK */
template<typename TObjectTypePointer>
inline void getObjects( PxMaterial*, PxPhysics& inPhysics, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inPhysics.getMaterials( inPointer, inObjCount );
}

/** Fill an array with the convex meshes in the SDK */
template<typename TObjectTypePointer>
inline void getObjects( PxConvexMesh*, PxPhysics& inPhysics, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inPhysics.getConvexMeshes( inPointer, inObjCount );
}
/** Fill an array with the convex meshes in the SDK */
template<typename TObjectTypePointer>
inline void getObjects( PxTriangleMesh*, PxPhysics& inPhysics, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inPhysics.getTriangleMeshes( inPointer, inObjCount );
}

/** Fill an array with the height fields in the SDK */
template<typename TObjectTypePointer>
inline void getObjects( PxHeightField*, PxPhysics& inPhysics, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inPhysics.getHeightFields( inPointer, inObjCount );
}

#if PX_USE_CLOTH_API
/** Fill an array with the cloth fabrics in the SDK */
template<typename TObjectTypePointer>
inline void getObjects( PxClothFabric*, PxPhysics& inPhysics, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inPhysics.getClothFabrics( inPointer, inObjCount );
}
#endif

/** Fill an array with the rigid statics in the scene */
template<typename TObjectTypePointer>
inline void getObjects( PxRigidStatic*, PxScene& inScene,TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inScene.getActors( PxActorTypeSelectionFlag::eRIGID_STATIC, reinterpret_cast<PxActor**>(inPointer), inObjCount );
}

/** Fill an array with the rigid dynamics in the scene */
template<typename TObjectTypePointer>
inline void getObjects( PxRigidDynamic*, PxScene& inScene, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inScene.getActors( PxActorTypeSelectionFlag::eRIGID_DYNAMIC, reinterpret_cast<PxActor**>(inPointer), inObjCount );
}

#if PX_USE_PARTICLE_SYSTEM_API
/** Fill an array with the particle systems in the scene */
template<typename TObjectTypePointer>
inline void getObjects( PxParticleSystem*, PxScene& inScene,TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inScene.getActors( PxActorTypeSelectionFlag::ePARTICLE_SYSTEM, reinterpret_cast<PxActor**>(inPointer), inObjCount );
}

/** Fill an array with the particle fluid in the scene */
template<typename TObjectTypePointer>
inline void getObjects( PxParticleFluid*, PxScene& inScene, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inScene.getActors( PxActorTypeSelectionFlag::ePARTICLE_FLUID, reinterpret_cast<PxActor**>(inPointer), inObjCount );
}
#endif // PX_USE_PARTICLE_SYSTEM_API

/** Fill an array with the articulations in the scene */
template<typename TObjectTypePointer>
inline void getObjects( PxArticulation*, PxScene& inScene, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inScene.getArticulations( inPointer, inObjCount );
}

/** Fill an array with the constraints in the scene. Ignore any constraints that are not joints. */
template<typename TObjectTypePointer>
inline void getObjects( PxConstraint*, PxScene& inScene, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	PxU32 objCount = getJointObjectAndCount( inPointer, inScene );
	PX_ASSERT( objCount == inObjCount );
	PX_UNUSED( objCount );
	PX_UNUSED( inObjCount );
}

#if PX_USE_CLOTH_API
/** Fill an array with the cloth in the scene */
template<typename TObjectTypePointer>
inline void getObjects( PxCloth*, PxScene& inScene, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inScene.getActors( PxActorTypeSelectionFlag::eCLOTH, reinterpret_cast<PxActor**>(inPointer), inObjCount );
}
#endif

/** Fill an array with the aggregates in the scene */
template<typename TObjectTypePointer>
inline void getObjects( PxAggregate*, PxScene& inScene, TObjectTypePointer& inPointer, PxU32 inObjCount )
{
	inScene.getAggregates( inPointer, inObjCount );
}

/** 
	Generic add objects of a particular type to a repx collection.
	\param crapPtr ignored, used for function overloading.
	\param inPhysics SDK or scene.
	\param inCollection RepX collection to fill up.
	\param inIdMap the repx id map to use.

*/
template<typename TObjectType, typename TPhysicsType>
inline void addItemsToRepX( TObjectType* crapPtr, TPhysicsType& inPhysics, RepXCollection& inCollection, RepXIdToRepXObjectMap& inIdMap )
{
	PxU32 objCount = getObjectCount( crapPtr, inPhysics );
	
	PxAllocatorCallback& callback = PxGetFoundation().getAllocatorCallback();
	
	TObjectType** theData = (TObjectType**)callback.allocate(objCount * sizeof( TObjectType* ), "", __FILE__, __LINE__); 
	if( !theData )
		return;

	getObjects( crapPtr, inPhysics, theData, objCount );

	for ( PxU32 idx = 0; idx < objCount; ++idx )
		addToRepXCollectionNF( inCollection, inIdMap, *theData[idx] );

	callback.deallocate(theData);
}

/**
	Add SDK or buffer items to a repx collection. 
	This function adds all RepX-supported items contained within the PxPhysics object.
	\param inPhysics Physics object.
	\param inIdMap id map to use.
	\param inCollection collection to fill.
*/
inline void addSDKItemsToRepX( PxPhysics& inPhysics, RepXIdToRepXObjectMap& inIdMap, RepXCollection& inCollection )
{
	//Run over all the buffers in physics, add them to collection
	addItemsToRepX( (PxMaterial*)NULL,		inPhysics, inCollection, inIdMap );
	addItemsToRepX( (PxConvexMesh*)NULL,	inPhysics, inCollection, inIdMap );
	addItemsToRepX( (PxTriangleMesh*)NULL,	inPhysics, inCollection, inIdMap );
	addItemsToRepX( (PxHeightField*)NULL,	inPhysics, inCollection, inIdMap );

#if PX_USE_CLOTH_API
	addItemsToRepX( (PxClothFabric*)NULL,	inPhysics, inCollection, inIdMap );
#endif
}

/**
	Add scene items to a repx collection.
	This function adds all RepX-supported items contained within the PxScene object.
	Note that this function adds objects in a defined order; joints are added last.  RepX
	does not take care of dependencies automatically which means that objects that are depending
	on other objects need to be added after those objects.  So this function needs to be called
	*after* addSDKItemsToRepX.
	\param inScene Scene object.
	\param inIdMap id map to use.
	\param inCollection collection to fill.
*/
inline void addSceneItemsToRepX( PxScene& inScene, RepXIdToRepXObjectMap& inIdMap, RepXCollection& inCollection )
{
	PxVec3 theUpVector = inScene.getGravity();
	theUpVector = -theUpVector;
	theUpVector.normalize();
	//Assume the up vector is the opposite of the gravity.
	inCollection.setUpVector( theUpVector );
	//Finally, run over all joints.
	addItemsToRepX( (PxRigidStatic*)NULL,		inScene, inCollection, inIdMap );
	addItemsToRepX( (PxRigidDynamic*)NULL,		inScene, inCollection, inIdMap );
	addItemsToRepX( (PxArticulation*)NULL,		inScene, inCollection, inIdMap );
	addItemsToRepX( (PxConstraint*)NULL,		inScene, inCollection, inIdMap );
	addItemsToRepX( (PxAggregate*)NULL,			inScene, inCollection, inIdMap );

#if PX_USE_PARTICLE_SYSTEM_API
	addItemsToRepX( (PxParticleSystem*)NULL,	inScene, inCollection, inIdMap );
	addItemsToRepX( (PxParticleFluid*)NULL,		inScene, inCollection, inIdMap );
#endif

#if PX_USE_CLOTH_API
	addItemsToRepX( (PxCloth*)NULL,				inScene, inCollection, inIdMap );
#endif

}

/**
	This function adds all RepX-supported items contained within the PxPhysics object and within the PxScene object.
	\param inPhysics Physics object.
	\param inScene Scene object.
	\param inIdMap id map to use.
	\param inCollection collection to fill.
*/
inline void addItemsToRepX( PxPhysics& inPhysics, PxScene& inScene, RepXIdToRepXObjectMap& inIdMap, RepXCollection& inCollection  )
{
	addSDKItemsToRepX( inPhysics, inIdMap, inCollection );
	addSceneItemsToRepX( inScene, inIdMap, inCollection );
}

/**Add repx items to a scene.  This runs over an instantiation result and based on either ignores
	the object or adds it to the scene. */
struct RepXCoreItemAdder
{
	PxScene* mScene;
	RepXCoreItemAdder( PxScene* inScene )
		: mScene( inScene )
	{
	}
	void operator()( const TRepXId /*inId*/, PxConvexMesh* ) {}
	void operator()( const TRepXId /*inId*/, PxTriangleMesh* ) {}
	void operator()( const TRepXId /*inId*/, PxHeightField* ) {}
	void operator()( const TRepXId /*inId*/, PxClothFabric* ) {}
	void operator()( const TRepXId /*inId*/, PxMaterial* ) {}
	void operator()( const TRepXId /*inId*/, PxRigidStatic* inActor )			{ mScene->addActor( *inActor ); }
	void operator()( const TRepXId /*inId*/, PxRigidDynamic* inActor )			{ mScene->addActor( *inActor ); }
	void operator()( const TRepXId /*inId*/, PxArticulation* inArticulation )	{ mScene->addArticulation( *inArticulation ); }
	//Joints are automatically added when their actors are set.
	void operator()( const TRepXId /*inId*/, PxJoint* /*inJoint*/ ) {}
	void operator()( const TRepXId /*inId*/, PxAggregate* inData ) { mScene->addAggregate( *inData); }

#if PX_USE_CLOTH_API
	void operator()( const TRepXId /*inId*/, PxCloth* inActor )					{ mScene->addActor( *inActor ); }
#endif

#if PX_USE_PARTICLE_SYSTEM_API
	void operator()( const TRepXId /*inId*/, PxParticleSystem* inActor )		{ mScene->addActor( *inActor ); }
	void operator()( const TRepXId /*inId*/, PxParticleFluid* inActor )			{ mScene->addActor( *inActor ); }
#endif

};

/** Run one operation type after a another operation type. */
template<typename TFirstOpType, typename TSecondOpType>
struct ComposingOperator
{
	TFirstOpType mFirst;
	TSecondOpType mSecond;
	ComposingOperator( TFirstOpType inFirst, TSecondOpType inSecond )
		: mFirst( inFirst )
		, mSecond( inSecond )
	{
	}
	template<typename TDataType>
	inline void operator()( const TRepXId inId, TDataType* inData ) 
	{
		mFirst( inId, inData );
		mSecond( inId, inData );
	}
};

/** Visit the joint types and apply an operator to them. */
template<typename TOperatorType>
struct JointRepXVisitor
{
	TOperatorType mOperator;
	JointRepXVisitor( TOperatorType inOp ) : mOperator( inOp ) {}
	template<typename TDataType>
	inline bool operator()( const TRepXId inId, TDataType* inData ) { mOperator( inId, inData ); return true; }
	inline bool operator()( const TRepXId, void*, const char*) { return false; }
};

/** Visit the core RepX types and apply an operator to them. */
template<typename TOperatorType>
struct CoreRepXVisitor
{
	TOperatorType mOperator;
	CoreRepXVisitor( TOperatorType inOp ) : mOperator( inOp ) {}
	template<typename TDataType>
	inline bool operator()( const TRepXId inId, TDataType* inData ) { mOperator( inId, inData ); return true; }
	inline bool operator()( const TRepXId inId, void* inLiveObject, const char* inRepXExtensionName) 
	{ 
		return visitJointRepXObject<bool>( inId, inLiveObject, inRepXExtensionName, JointRepXVisitor<TOperatorType>(mOperator) );
	}
};

/**
	A generic instantiation handler that visits the repx objects after they have been
	instantiated.  Note that this doesn't visit the joints.
*/
template<typename TOperatorType>
struct GenericInstantiationHandler : public RepXInstantiationResultHandler
{
	TOperatorType mOperator;
	GenericInstantiationHandler( TOperatorType inOperator ) : mOperator( inOperator ) {}
	virtual void addInstantiationResult( RepXInstantiationResult inResult ) 
	{
		visitCoreRepXObject<bool>( inResult.mCollectionId, inResult.mLiveObject, inResult.mExtensionName, CoreRepXVisitor<TOperatorType>(mOperator) );
	}
};
/**
	Instantiate a repx collection running an operator over each instantiation result.
	
	Since we are creating the id map ourselves, we can assume that the only valid option for
	inAddOriginalIdsToObjectMap is true.  Thus it doesn't need to be passed in.

	\param inCollection RepX collection to instantiate.
	\param inPhysics Physics SDK object to use.
	\param inCooking cooking system to cook the mesh buffers.
	\param inStringTable string table used for instantiation.
	\param inIdMap Id map to use.
	\param inAddIdsToInputIdMap True if the instantiated object ids should be added to the input id map.  
 */
template<typename TOperator>
inline RepXErrorCode::Enum  instantiateCollection( RepXCollection& inCollection, PxPhysics& inPhysics, PxCooking& inCooking, PxStringTable* inStringTable, TOperator inInstantiationOperator, RepXIdToRepXObjectMap* inIdMap = NULL, bool inAddIdsToInputIdMap = false )
{
	RepXIdToRepXObjectMap* theMap = NULL;
	if ( NULL == inIdMap )
		theMap = RepXIdToRepXObjectMap::create(PxGetFoundation().getAllocatorCallback());
	else
		theMap = inAddIdsToInputIdMap ? inIdMap : inIdMap->clone();
	PX_ASSERT( NULL != theMap );

	RepXInstantiationArgs theInstantiationArgs( &inCooking, &inPhysics, inStringTable );
	GenericInstantiationHandler<TOperator> theHandler( inInstantiationOperator );
	RepXErrorCode::Enum ret = inCollection.instantiateCollection( theInstantiationArgs, theMap, &theHandler );
	if ( inIdMap != theMap ) theMap->destroy();

	return ret;
}

/**
	Instantiate a repx file and add objects to a scene.  This function assumes that any SDK level objects
	the instantiation requires are in the RepX file before the scene level objects.
	\param inCollection RepX collection.
	\param inPhysics SDK.
	\param inCooking Cooking object to use.
	\param inScene scene to add objects to.
	\param inStringTable string table to use for strings in RepX file (like object names).
	\param inIdMap Id map to use.
	\param inAddIdsToInputIdMap True if the instantiated object ids should be added to the input id map.
*/
inline RepXErrorCode::Enum addObjectsToScene( RepXCollection& inCollection, PxPhysics& inPhysics, PxCooking& inCooking, PxScene& inScene, PxStringTable* inStringTable, RepXIdToRepXObjectMap* inIdMap = NULL, bool inAddIdsToInputIdMap = false )
{
	return instantiateCollection( inCollection, inPhysics, inCooking, inStringTable, RepXCoreItemAdder( &inScene ), inIdMap, inAddIdsToInputIdMap );
}

/**
	Instantiate a RepX collection and add the instantiation results to a PxCollection.
*/
struct RepXPxCollectionCoreItemAdder
{
	PxCollection* mBufferCollection;
	PxCollection* mSceneCollection;
	PxUserReferences *mRefCollection;
	RepXPxCollectionCoreItemAdder( PxCollection* bufCol, PxCollection* sceneCol, PxUserReferences *refCol )
		: mBufferCollection( bufCol )
		, mSceneCollection( sceneCol )
		, mRefCollection(refCol)
	{
	}

	void addBuffer( const TRepXId inId, PxSerializable* item )
	{
		if ( mRefCollection )
		{
			mRefCollection->setUserData(item, PxSerialObjectRef( inId ));
		}
		item->collectForExport( *mBufferCollection );
		mBufferCollection->setObjectRef( *item, PxSerialObjectRef( inId ) );
		mSceneCollection->addExternalRef( *item, PxSerialObjectRef( inId ) );
	}

	void addSceneObject( const TRepXId inId, PxSerializable* item ) 
	{
		item->collectForExport( *mSceneCollection );
		mSceneCollection->setObjectRef( *item, PxSerialObjectRef( inId ) );
	}
	void operator()( const TRepXId inId, PxClothFabric* data ) { addBuffer( inId, data ); }
	void operator()( const TRepXId inId, PxConvexMesh* mesh )  { addBuffer( inId, mesh ); }
	void operator()( const TRepXId inId, PxTriangleMesh* mesh)  { addBuffer( inId, mesh ); }
	void operator()( const TRepXId inId, PxHeightField* mesh) { addBuffer( inId, mesh ); }
	void operator()( const TRepXId inId, PxMaterial* material ) { addBuffer( inId, material ); }
	void operator()( const TRepXId inId, PxRigidStatic* inActor ) { addSceneObject( inId, inActor ); }
	void operator()( const TRepXId inId, PxRigidDynamic* inActor ) { addSceneObject( inId, inActor ); }
	void operator()( const TRepXId inId, PxArticulation* inArticulation ) { addSceneObject( inId, inArticulation ); }
	void operator()( const TRepXId inId, PxJoint* inJoint ) { addSceneObject( inId, inJoint ); }
	void operator()( const TRepXId inId, PxAggregate* inActor ) {  addSceneObject( inId, reinterpret_cast<PxSerializable*>(inActor)); }
#if PX_USE_CLOTH_API
	void operator()( const TRepXId inId, PxCloth* inCloth ) { addSceneObject( inId, inCloth ); }
#endif
#if PX_USE_PARTICLE_SYSTEM_API
	void operator()( const TRepXId inId, PxParticleSystem* inParticleSystem ) { addSceneObject( inId, inParticleSystem ); }
	void operator()( const TRepXId inId, PxParticleFluid* inParticleFluid ) { addSceneObject( inId, inParticleFluid ); }
#endif
};

/**
	repx->pxcollection in a manner that allows you to insert the same collection into the same scene multiple times.
	\param srcRepxCollection source collection to instantiate.
	\param inPhysics Physics SDK.
	\param inCooking cooking object used to instantiate RepX collection.
	\param inStringTable string table used for object names.
	\param outBuffers The PxCollection that will contain the SDK-level objects.
	\param outSceneObjects the PxCollection that will contain the PxScene-level objects.
	\param outUserRefs the PxUserReferences to which ids of outBuffer serializables are written to.
	\param inIdMap Id map to use.
	\param inAddIdsToInputIdMap True if the instantiated object ids should be added to the input id map.
 */
inline RepXErrorCode::Enum addObjectsToPxCollection( RepXCollection& srcRepxCollection
												, PxPhysics& inPhysics
												, PxCooking& inCooking
												, PxStringTable* inStringTable
												, PxCollection& outBuffers
												, PxCollection& outSceneObjects
												, PxUserReferences* outUserRefs = NULL
												, RepXIdToRepXObjectMap* inIdMap = NULL
												, bool inAddIdsToInputIdMap = false )
{
	return instantiateCollection( srcRepxCollection, inPhysics, inCooking, inStringTable, RepXPxCollectionCoreItemAdder( &outBuffers, &outSceneObjects, outUserRefs ), inIdMap, inAddIdsToInputIdMap );
}

/**
 *	Save a scene to RepX.  This will place all SDK objects and PxScene objects into the same
 *	RepX file.
 */
inline void saveSceneToRepX( PxPhysics& inPhysics, PxScene& inScene, RepXCollection& inCollection )
{
	RepXIdToRepXObjectMap* theIdMap( RepXIdToRepXObjectMap::create(PxGetFoundation().getAllocatorCallback()) );
	addItemsToRepX( inPhysics, inScene, *theIdMap, inCollection );
	theIdMap->destroy();
}

/**
	Build the repx extensions list used when constructing a RepX collection.
	\param inExtensionBuffer place to store created extensions.
	\param inBufferSize should be greater than getNumCoreExtensions() + getNumJointExtensions();
	\param inCallback allocator used to allocator extensions.
	\return Number of extensions created.
*/
inline PxU32 buildExtensionList( RepXExtension** inExtensionBuffer, PxU32 inBufferSize, PxAllocatorCallback& inCallback )
{
	PX_ASSERT( inBufferSize > getNumCoreExtensions() + getNumJointExtensions() );
	PxU32 totalCreated = 0;
	PxU32 numCreated = createCoreExtensions( inExtensionBuffer, inBufferSize, inCallback );
	totalCreated += numCreated;
	inBufferSize -= numCreated;
	numCreated = createJointExtensions( inExtensionBuffer + numCreated, inBufferSize, inCallback );
	totalCreated += numCreated;
	return totalCreated;
}

/**
	Create a repx collection and setup the extensions.
	\param inScale the scale the collection is created at.
	\param inCallback allocator to use for collection and extensions.
	\return an empty RepX collection.
*/
inline RepXCollection* createCollection( const PxTolerancesScale& inScale, PxAllocatorCallback& inCallback )
{
	RepXExtension* theExtensions[64];
	PxU32 numExtensions = buildExtensionList( theExtensions, 64, inCallback );
	return RepXCollection::create( theExtensions, numExtensions, inScale, inCallback );
}
/**
	Create a repx collection and setup the extensions.  This uses the default PxFoundation allocator.
	\param inScale the scale the collection is created at.
	\return an empty RepX collection.
*/
inline RepXCollection* createCollection( const PxTolerancesScale& inScale )
{
	return createCollection( inScale, PxGetFoundation().getAllocatorCallback() );
}

/**
	Create a repx collection, load data from this data source, and upgrade the collection
	if loading an older collection.
	\param data data source.
	\param inCallback allocator to use for RepX.
	\return Collection of loaded data.
*/
inline RepXCollection* createCollection( PxInputData& data, PxAllocatorCallback& inCallback )
{
	RepXExtension* theExtensions[64];
	PxU32 numExtensions = buildExtensionList( theExtensions, 64, inCallback );
	RepXCollection* retval = RepXCollection::create( data, theExtensions, numExtensions, inCallback );
	if ( retval )
		retval = &RepXUpgrader::upgradeCollection( *retval );
	return retval;
}

/**
	Create a repx collection from a PxCollection
	\param inPxCollection source collection to instantiate.
	\param inAnonymousNameStart the start address of references of objects. It will be auto incremented by 1.
	\param inScale the scale the collection is created at.
	\param inCallback allocator to use for RepX.
	\return a repx collection created from a PxCollection.
*/
inline RepXCollection* pxCollectionToRepXCollection( PxCollection& inPxCollection, PxU64& inAnonymousNameStart, const PxTolerancesScale& inScale, PxAllocatorCallback& inCallback )
{
	return RepXCollection::create( inPxCollection, inAnonymousNameStart, inScale, inCallback );
}

/**
	Serialize a PxCollection to the stream saved as repx format.
	\param outStream Serialize a PxCollection to the stream.
	\param inPxCollection source collection to instantiate.	
	\param inAnonymousNameStart the start address of references of objects. It will be auto incremented by 1.	
*/
inline void serializeToRepX( PxOutputStream& outStream, PxCollection& inPxCollection, PxU64& inAnonymousNameStart )
{
	RepXCollection *r = pxCollectionToRepXCollection(inPxCollection, inAnonymousNameStart, PxGetPhysics().getTolerancesScale(), PxGetFoundation().getAllocatorCallback());
	r->save(outStream);
	r->destroy();
}

/**
	Deserialize the stream saved as repx format to PxCollection.
	\param inputStream Deserialize the stream to PxCollection.
	\param inPhysics Physics object.
	\param inCooking cooking system to cook the mesh buffers.
	\param inStringTable string table used for object names.
	\param inExternalRefs external references
	\param outBuffers The PxCollection that will contain the SDK-level objects. 
	\param outSceneObjects the PxCollection that will contain the PxScene-level objects.
	\param userRefs the PxUserReferences to which ids of outBuffers serializables are written to.
*/
inline RepXErrorCode::Enum deserializeFromRepX(PxInputData &inputStream
						, PxPhysics& inPhysics
						, PxCooking& inCooking
						, PxStringTable* inStringTable
						, const PxUserReferences* inExternalRefs
						, PxCollection& outBuffers 
						, PxCollection& outSceneObjects
						, PxUserReferences* userRefs = NULL)
{
	PxAllocatorCallback& theAllocator = PxGetFoundation().getAllocatorCallback();

	RepXCollection* theRepxCollection = createCollection(inputStream, theAllocator );
	RepXErrorCode::Enum ret = RepXCollection::repXCollectionToPxCollections(*theRepxCollection, inPhysics, inCooking, theAllocator, inStringTable, inExternalRefs, outBuffers, outSceneObjects, userRefs);
	theRepxCollection->destroy();

	return ret;
}
}}
#endif