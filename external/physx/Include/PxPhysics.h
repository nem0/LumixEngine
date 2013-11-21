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


#ifndef PX_PHYSICS_NX_PHYSICS
#define PX_PHYSICS_NX_PHYSICS


/** \addtogroup physics
@{
*/

#include "PxPhysX.h"
#include "common/PxSerialFramework.h"
#include "foundation/PxTransform.h"
#if PX_USE_CLOTH_API
#include "cloth/PxClothTypes.h"
#include "cloth/PxClothFabricTypes.h"
#endif

namespace physx { namespace debugger { namespace comm {
	class PvdConnectionManager;
}}}

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxInputStream;
class PxOutputStream;

class PxRigidActor;
class PxConstraintConnector;
struct PxConstraintShaderTable;

class PxProfileZone;
class PxProfileZoneManager;

class PxGeometry;

struct PxCookingValue
{
	enum Enum
	{
		/**
		Version numbers follow this format:

		Version = 16bit|16bit

		The high part is increased each time the format changes so much that
		pre-cooked files become incompatible with the system (and hence must
		be re-cooked)

		The low part is increased each time the format changes but the code
		can still read old files. You don't need to re-cook the data in that
		case, unless you want to make sure cooked files are optimal.
		*/
		eCONVEX_VERSION_PC,
		eMESH_VERSION_PC,
		eCONVEX_VERSION_XENON,
		eMESH_VERSION_XENON,
		eCONVEX_VERSION_PLAYSTATION3,
		eMESH_VERSION_PLAYSTATION3,
	};
};

/**
\brief Abstract singleton factory class used for instancing objects in the Physics SDK.

In addition you can use PxPhysics to set global parameters which will effect all scenes,
create triangle meshes.

You can get an instance of this class by calling PxCreatePhysics().

@see PxCreatePhysics() PxScene PxVisualizationParameter PxTriangleMesh PxConvexMesh
*/
class PxPhysics
{
public:

	virtual ~PxPhysics() {};

	virtual bool registerClass(PxType type, PxClassCreationCallback callback) = 0;

	/**
	\brief Creates a user references object.

	User references are needed when a collection contains external references, either to
	another collection (when serializing subsets) or to user objects.

	@see PxUserReferences::release() PxUserReferences
	*/
	virtual	PxUserReferences*	createUserReferences() = 0;

	PX_DEPRECATED virtual	void				releaseUserReferences(PxUserReferences& ref) = 0;

	/**
	\brief Creates a collection object.

	Objects can only be serialized or deserialized through a collection.
	For serialization, users must add objects to the collection and serialize the collection as a whole.
	For deserialization, the system gives back a collection of deserialized objects to users.

	\return The new collection object.

	@see PxCollection::release()
	*/
	virtual	PxCollection*		createCollection() = 0;

	PX_DEPRECATED virtual	void				releaseCollection(PxCollection&) = 0;

	/**
	\brief Adds collected objects to a scene.

	This function adds all objects contained in the input collection to the input scene. This is
	typically used after deserializing the collection, to populate the scene with deserialized
	objects.

	\param[in] collection Objects to add to the scene. See #PxCollection
	\param[in] scene Scene to which collected objects will be added. See #PxScene

	@see PxCollection PxScene
	*/
	virtual	void				addCollection(const PxCollection& collection, PxScene& scene) = 0;

	/**
	\brief Destroys the instance it is called on.

	Use this release method to destroy an instance of this class. Be sure
	to not keep a reference to this object after calling release.
	Avoid release calls while a scene is simulating (in between simulate() and fetchResults() calls).

	Note that this must be called once for each prior call to PxCreatePhysics, as
	there is a reference counter. Also note that you mustn't destroy the allocator or the error callback (if available) until after the
	reference count reaches 0 and the SDK is actually removed.

	Releasing an SDK will also release any scenes, triangle meshes, convex meshes, and heightfields
	created through it, provided the user hasn't already done so.

	@see PxCreatePhysics()
	*/
	virtual	void release() = 0;

	/**
	\brief Creates a scene.

	The scene can then create its contained entities.

	\param[in] sceneDesc Scene descriptor. See #PxSceneDesc
	\return The new scene object.

	@see PxScene PxScene.release() PxSceneDesc
	*/
	virtual PxScene*			createScene(const PxSceneDesc& sceneDesc) = 0;

	/**
	\brief Gets number of created scenes.

	\return The number of scenes created.

	@see getScene()
	*/
	virtual PxU32				getNbScenes()			const	= 0;

	/**
	\brief Writes the array of scene pointers to a user buffer.
	
	Returns the number of pointers written.

	The ordering of the scene pointers in the array is not specified.

	\param[out] userBuffer The buffer to receive scene pointers.
	\param[in] bufferSize The number of scene pointers which can be stored in the buffer.
	\param[in] startIndex Index of first scene pointer to be retrieved
	\return The number of scene pointers written to userBuffer, this should be less or equal to bufferSize.

	@see getNbScenes() PxScene
	*/
	virtual	PxU32				getScenes(PxScene** userBuffer, PxU32 bufferSize, PxU32 startIndex=0) const = 0;


	/**
	\brief Creates a static rigid actor with the specified pose and all other fields initialized
	to their default values.
	
	\param[in] pose the initial pose of the actor. Must be a valid transform

	@see PxRigidStatic
	*/

	virtual PxRigidStatic*      createRigidStatic(const PxTransform& pose) = 0;



	/**
	\brief Creates a dynamic rigid actor with the specified pose and all other fields initialized
	to their default values.
	
	\param[in] pose the initial pose of the actor. Must be a valid transform

	@see PxRigidDynamic
	*/

	virtual PxRigidDynamic*      createRigidDynamic(const PxTransform& pose) = 0;



	/**
	\brief Creates a constraint shader.

	\note A constraint shader will get added automatically to the scene the two linked actors belong to. Either, but not both, of actor0 and actor1 may
	be NULL to denote attachment to the world.
	
	\param[in] actor0 the first actor
	\param[in] actor1 the second actor
	\param[in] connector the connector object, which the SDK uses to communicate with the infrastructure for the constraint
	\param[in] shaders the shader functions for the constraint
	\param[in] dataSize the size of the data block for the shader

	\return The new shader.

	*/
	virtual PxConstraint*      createConstraint(PxRigidActor* actor0, PxRigidActor* actor1, PxConstraintConnector& connector, const PxConstraintShaderTable& shaders, PxU32 dataSize)		= 0;


	/**
	\brief Creates an articulation with all fields initialized to their default values.
	
	\return the new articulation

	@see PxRigidDynamic
	*/

	virtual PxArticulation*      createArticulation() = 0;

	/**
	\brief Creates an aggregate with the specified maximum size and selfCollision property.

	\param[in] maxSize the maximum number of actors that may be placed in the aggregate.  This value must not exceed 128, otherwise NULL will be returned.
	\param[in] enableSelfCollision whether the aggregate supports self-collision
	\return The new aggregate.

	@see PxAggregate
	*/
	virtual	PxAggregate*		createAggregate(PxU32 maxSize, bool enableSelfCollision)	= 0;

#if PX_USE_PARTICLE_SYSTEM_API
	/**
	\brief Creates a particle system.

	\param maxParticles the maximum number of particles that may be placed in the particle system
	\param perParticleRestOffset whether the ParticleSystem supports perParticleRestOffset
	\return The new particle system.
	*/
	virtual PxParticleSystem*	createParticleSystem(PxU32 maxParticles, bool perParticleRestOffset = false) = 0;

	/**
	\brief Creates a particle fluid. 
	
	\param maxParticles the maximum number of particles that may be placed in the particle fluid
	\param perParticleRestOffset whether the ParticleFluid supports perParticleRestOffset
	\return The new particle fluid.
	*/
	virtual PxParticleFluid*	createParticleFluid(PxU32 maxParticles, bool perParticleRestOffset = false) = 0;
#endif


#if PX_USE_CLOTH_API
	/**
	Creates a cloth.

	\param globalPose The world space transform of the cloth.
	\param fabric The fabric the cloth should use.
	\param particles Particle definition buffer.
	The size of the buffer has to match the number of points of the cloth mesh which   elements must match with the provided 
	\param collData Collision information.
	\param flags Cloth flags.
	\return The new cloth.

	@see PxCloth PxClothFabric PxClothCollisionData PxClothFlags
	*/
	virtual PxCloth*			createCloth(const PxTransform& globalPose, PxClothFabric& fabric, const PxClothParticle* particles, const PxClothCollisionData& collData, PxClothFlags flags) = 0;
#endif

	/**
	\brief Creates a new material with default properties.

	\return The new material.

	\param staticFriction the coefficient of static friction
	\param dynamicFriction the coefficient of dynamic friction
	\param restitution the coefficient of restitution

	@see PxMaterial 
	*/
	virtual PxMaterial*        createMaterial(PxReal staticFriction, PxReal dynamicFriction, PxReal restitution)		= 0;


	/**
	\brief Return the number of materials that currently exist.

	\return Number of materials.

	@see getMaterials()
	*/
	virtual PxU32				getNbMaterials() const = 0;

	/**
	\brief Writes the array of material pointers to a user buffer.
	
	Returns the number of pointers written.

	The ordering of the materials in the array is not specified.

	\param[out] userBuffer The buffer to receive material pointers.
	\param[in] bufferSize The number of material pointers which can be stored in the buffer.
	\param[in] startIndex Index of first material pointer to be retrieved
	\return The number of material pointers written to userBuffer, this should be less or equal to bufferSize.

	@see getNbMaterials() PxMaterial
	*/
	virtual	PxU32				getMaterials(PxMaterial** userBuffer, PxU32 bufferSize, PxU32 startIndex=0) const = 0;

	/**
	\brief Creates a triangle mesh object.

	This can then be instanced into #PxShape objects.

	\param[in] stream The triangle mesh stream.
	\return The new triangle mesh.

	@see PxTriangleMesh PxTriangleMesh.release() PxInputStream
	*/
	virtual PxTriangleMesh*    createTriangleMesh(PxInputStream& stream)					= 0;
	

	/**
	\brief Return the number of triangle meshes that currently exist.

	\return Number of triangle meshes.

	@see getTriangleMeshes()
	*/
	virtual PxU32				getNbTriangleMeshes() const = 0;

	/**
	\brief Writes the array of triangle mesh pointers to a user buffer.
	
	Returns the number of pointers written.

	The ordering of the triangle meshes in the array is not specified.

	\param[out] userBuffer The buffer to receive triangle mesh pointers.
	\param[in] bufferSize The number of triangle mesh pointers which can be stored in the buffer.
	\param[in] startIndex Index of first mesh pointer to be retrieved
	\return The number of triangle mesh pointers written to userBuffer, this should be less or equal to bufferSize.

	@see getNbTriangleMeshes() PxTriangleMesh
	*/
	virtual	PxU32				getTriangleMeshes(PxTriangleMesh** userBuffer, PxU32 bufferSize, PxU32 startIndex=0) const = 0;

	/**
	\brief Creates a PxHeightField object.

	This can then be instanced into #PxShape objects.

	\param[in] heightFieldDesc The descriptor to load the object from.
	\return The new height field object.

	@see PxHeightField PxHeightField.release() PxHeightFieldDesc PxHeightFieldGeometry PxShape
	*/
	virtual PxHeightField*		createHeightField(const PxHeightFieldDesc& heightFieldDesc) = 0;

	/**
	\brief Return the number of heightfields that currently exist.

	\return Number of heightfields.

	@see getHeightFields()
	*/
	virtual PxU32				getNbHeightFields() const = 0;

	/**
	\brief Writes the array of heightfield pointers to a user buffer.
	
	Returns the number of pointers written.

	The ordering of the heightfields in the array is not specified.

	\param[out] userBuffer The buffer to receive heightfield pointers.
	\param[in] bufferSize The number of heightfield pointers which can be stored in the buffer.
	\param[in] startIndex Index of first heightfield pointer to be retrieved
	\return The number of heightfield pointers written to userBuffer, this should be less or equal to bufferSize.

	@see getNbHeightFields() PxHeightField
	*/
	virtual	PxU32				getHeightFields(PxHeightField** userBuffer, PxU32 bufferSize, PxU32 startIndex=0) const = 0;

	/**
	\brief Creates a convex mesh object.

	This can then be instanced into #PxShape objects.

	\param[in] stream The stream to load the convex mesh from.
	\return The new convex mesh.

	@see PxConvexMesh PxConvexMesh.release() PxInputStream createTriangleMesh() PxConvexMeshGeometry PxShape
	*/
	virtual PxConvexMesh*		createConvexMesh(PxInputStream &stream)					= 0;

	/**
	\brief Return the number of convex meshes that currently exist.

	\return Number of convex meshes.

	@see getConvexMeshes()
	*/
	virtual PxU32				getNbConvexMeshes() const = 0;

	/**
	\brief Writes the array of convex mesh pointers to a user buffer.
	
	Returns the number of pointers written.

	The ordering of the convex meshes in the array is not specified.

	\param[out] userBuffer The buffer to receive convex mesh pointers.
	\param[in] bufferSize The number of convex mesh pointers which can be stored in the buffer.
	\param[in] startIndex Index of first convex mesh pointer to be retrieved
	\return The number of convex mesh pointers written to userBuffer, this should be less or equal to bufferSize.

	@see getNbConvexMeshes() PxConvexMesh
	*/
	virtual	PxU32				getConvexMeshes(PxConvexMesh** userBuffer, PxU32 bufferSize, PxU32 startIndex=0) const = 0;


#if PX_USE_CLOTH_API
	/**
	\brief Creates a cloth fabric object.

	This can then be instanced into #PxCloth objects.

	\param[in] stream The stream to load the cloth fabric from.
	\return The new cloth fabric.

	@see PxClothFabric PxClothFabric.release() PxInputStream PxCloth
	*/
	virtual PxClothFabric*		createClothFabric(PxInputStream& stream) = 0;

	/**
	\brief Creates a cloth fabric object from particle connectivity and restlength information.

	This can then be instanced into #PxCloth objects.

	\note We recommended using #PxCooking.cookClothFabric() to create cloth fabrics from meshes and then using createClothFabric(const PxInputStream& stream).
	This method should only be used if you need to provide fully customized particle fiber/connectivity information for your fabric or if you did custom
	cloth fabric serialization and want to deserialize.

	\param[in] nbParticles the number of particles needed when creating a PxCloth instance from the fabric.
	\param[in] nbPhases the number of solver phases.
	\param[in] phases array defining which set to use for each phase. See #PxClothFabric.getPhases().
	\param[in] phaseTypes array defining the type of each phase. See #PxClothFabricPhaseType.
	\param[in] nbRestvalues the number of rest values
	\param[in] restvalues array of rest values for each constraint. See #PxClothFabric.getRestvalues().
	\param[in] nbSets number of sets in the fabric.
	\param[in] sets array with an index per set which points one entry beyond the last fiber of the set. See #PxClothFabric.getSets().
	\param[in] fibers array with an index per fiber which points one entry beyond the last particle index of the fiber. See #PxClothFabric.getFibers().
	\param[in] indices array of particle indices which specifies the line strips of connected particles of a fiber. See #PxClothFabric.getParticleIndices().
	\return The new cloth fabric.

	@see PxClothFabric PxClothFabric.release() PxCloth
	*/
	virtual PxClothFabric*		createClothFabric(PxU32 nbParticles, PxU32 nbPhases, const PxU32* phases, 
									const PxClothFabricPhaseType::Enum* phaseTypes, PxU32 nbRestvalues, const PxReal* restvalues, 
									PxU32 nbSets, const PxU32* sets, const PxU32* fibers,  const PxU32* indices) = 0;

	/**
	\brief Return the number of cloth fabrics that currently exist.

	\return Number of cloth fabrics.

	@see getClothFabrics()
	*/
	virtual PxU32				getNbClothFabrics() const = 0;

	/**
	\brief Writes the array of cloth fabrics to a user buffer.
	
	Returns the number of pointers written.

	The ordering of the cloth fabrics in the array is not specified.

	\param[out] userBuffer The buffer to receive cloth fabric pointers.
	\param[in] bufferSize The number of cloth fabric pointers which can be stored in the buffer.
	\return The number of cloth fabric pointers written to userBuffer, this should be less or equal to bufferSize.

	@see getNbClothFabrics() PxClothFabric
	*/
	virtual	PxU32				getClothFabrics(PxClothFabric** userBuffer, PxU32 bufferSize) const = 0;
#endif

	/**
	\brief Returns the simulation tolerance parameters.  
	\return The current simulation tolerance parameters.  
	*/
	virtual const PxTolerancesScale&		getTolerancesScale() const = 0;

	/**
	\brief Retrieves the Foundation instance.
	\return A reference to the Foundation object.
	*/
	virtual PxFoundation&		getFoundation() = 0;

	/**
	\brief Retrieves the PhysX Visual Debugger.
	\return A pointer to the PxVisualDebugger. Can be NULL if PVD is not supported on this platform.
	*/
	virtual PxVisualDebugger*	getVisualDebugger()	= 0;

	/**
		The factory manager allows notifications when a new
		connection to pvd is made.  It also allows the users to specify
		a scheme to handle the read-side of a network connection.  By default, 
		the SDK specifies that a thread gets launched which blocks reading
		on the network socket.
	
		\return A valid manager *if* the SDK was compiled with PVD support.  Null otherwise.
	*/
	virtual physx::debugger::comm::PvdConnectionManager* getPvdConnectionManager() = 0;

	/*
	\brief Retrieves the profile sdk manager.
	*	The profile sdk manager manages collections of SDKs and objects that are interested in 
	*	receiving events from them.  This is the hook if you want to write the profiling events
	*	from multiple SDK's out to a file.
	\return The SDK's profiling system manager.
	*/
	virtual physx::PxProfileZoneManager* getProfileZoneManager() = 0;

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/**
\brief Reads an internal value (cooking format version).

\param[in] cookValue See #PxCookingValue
*/
PX_C_EXPORT PX_PHYSX_CORE_API physx::PxU32 PX_CALL_CONV PxGetValue(physx::PxCookingValue::Enum cookValue);


/**
\brief Registers optional components for articulations.
*/

PX_C_EXPORT PX_PHYSX_CORE_API void PX_CALL_CONV PxRegisterArticulations(physx::PxPhysics& physics);


/**
\brief Registers optional components for height field collision.
*/
PX_C_EXPORT PX_PHYSX_CORE_API void PX_CALL_CONV PxRegisterHeightFields(physx::PxPhysics& physics);


/**
\brief Creates an instance of the physics SDK with minimal additional components registered

Creates an instance of this class. May not be a class member to avoid name mangling.
Pass the constant PX_PHYSICS_VERSION as the argument.
There may be only one instance of this class per process. Calling this method after an instance 
has been created already will result in an error message and NULL will be returned.

\param version Version number we are expecting(should be PX_PHYSICS_VERSION)
\param foundation Foundation instance (see #PxFoundation)
\param scale values used to determine default tolerances for objects at creation time
\param trackOutstandingAllocations true if you want to track memory allocations 
			so a debugger connection partway through your physics simulation will get
			an accurate map of everything that has been allocated so far.  This could have a memory
			and performance impact on your simulation hence it defaults to off.
\param profileZoneManager If profiling information is required, a profile zone manager has to be provided.
\return PxPhysics instance on success, NULL if operation failed
*/

PX_C_EXPORT PX_PHYSX_CORE_API physx::PxPhysics* PX_CALL_CONV PxCreateBasePhysics(physx::PxU32 version,
																			     physx::PxFoundation& foundation,
																			     const physx::PxTolerancesScale& scale,
																			     bool trackOutstandingAllocations = false,
																				 physx::PxProfileZoneManager* profileZoneManager = NULL);

/**
\brief Creates an instance of the physics SDK.

Creates an instance of this class. May not be a class member to avoid name mangling.
Pass the constant PX_PHYSICS_VERSION as the argument.
There may be only one instance of this class per process. Calling this method after an instance 
has been created already will result in an error message and NULL will be returned.

\param version Version number we are expecting(should be PX_PHYSICS_VERSION)
\param foundation Foundation instance (see #PxFoundation)
\param scale values used to determine default tolerances for objects at creation time
\param trackOutstandingAllocations true if you want to track memory allocations 
			so a debugger connection partway through your physics simulation will get
			an accurate map of everything that has been allocated so far.  This could have a memory
			and performance impact on your simulation hence it defaults to off.
\param profileZoneManager If profiling information is required, a profile zone manager has to be provided.
\return PxPhysics instance on success, NULL if operation failed

\see PxCreatePhysics
*/

PX_INLINE physx::PxPhysics* PxCreatePhysics(physx::PxU32 version,
											physx::PxFoundation& foundation,
											const physx::PxTolerancesScale& scale,
											bool trackOutstandingAllocations = false,
											physx::PxProfileZoneManager* profileZoneManager = NULL)
{
	physx::PxPhysics* physics = PxCreateBasePhysics(version, foundation, scale, trackOutstandingAllocations, profileZoneManager);
	if(!physics)
		return NULL;

	PxRegisterArticulations(*physics);
	PxRegisterHeightFields(*physics);
	return physics;
}

/**
\brief Retrieves the PhysX SDKmetadata.

Before using this function the user must call #PxCreatePhysics(). If #PxCreatePhysics()
has not been called then NULL will be returned.
*/

PX_C_EXPORT PX_PHYSX_CORE_API void PX_CALL_CONV PxGetSDKMetaData(const physx::PxPhysics& physics, physx::PxOutputStream& stream);

/**
\brief Retrieves the Physics SDK after it has been created.

Before using this function the user must call #PxCreatePhysics().

\note The behavior of this method is undefined if the Physics SDK instance has not been created already.
*/
PX_C_EXPORT PX_PHYSX_CORE_API physx::PxPhysics& PX_CALL_CONV PxGetPhysics();


/** @} */
#endif
