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


#ifndef PX_COOKING_H
#define PX_COOKING_H
/** \addtogroup cooking
@{
*/
#include "common/PxPhysXCommonConfig.h"
#include "common/PxTolerancesScale.h"
#include "cooking/Pxc.h"

#include "cooking/PxConvexMeshDesc.h"
#include "cooking/PxTriangleMeshDesc.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxOutputStream;
class PxBinaryConverter;
class PxPhysicsInsertionCallback;

struct PxPlatform
{
	enum Enum
	{
		ePC,
		eXENON,
		ePLAYSTATION3,
		eARM,
		eWIIU
	};
};

/**
\brief Result from convex cooking.
*/
struct PxConvexMeshCookingResult
{
	enum Enum
	{
		/**
		\brief Convex mesh cooking succeeded.
		*/
		eSUCCESS,

		/**
		\brief Convex mesh cooking failed, algorithm couldn't find 4 initial vertices without a small triangle.

		@see PxCookingParams::areaTestEpsilon PxConvexFlag::eCHECK_ZERO_AREA_TRIANGLES
		*/
		eZERO_AREA_TEST_FAILED,

		/**
		\brief Something unrecoverable happened. Check the error stream to find out what.
		*/
		eFAILURE
	};
};

/**

\brief Enum for the set of mesh pre-processing parameters.

*/

struct PxMeshPreprocessingFlag
{
	enum Enum
	{
		/**
		\brief When set, mesh welding is performed. See PxCookingParams::meshWeldTolerance. Clean mesh must be enabled.
		*/
		eWELD_VERTICES					=	1 << 0, 

		/**
		\brief When set, unreferenced vertices are removed during clean mesh. Clean mesh must be enabledt.
		*/
		PX_DEPRECATED	eREMOVE_UNREFERENCED_VERTICES	=	1 << 1,

		/**
		\brief When set, duplicit vertices are removed during clean mesh. Clean mesh must be enabled.
		*/
		PX_DEPRECATED   eREMOVE_DUPLICATED_TRIANGLES	=	1 << 2,

		/**
		\brief When set, mesh cleaning is disabled. This makes cooking faster.

		When clean mesh is not performed, mesh welding is also not performed. 

		It is recommended to use only meshes that passed during validateTriangleMesh. 

		*/
		eDISABLE_CLEAN_MESH								=	1 << 3, 

		/**
		\brief When set, active edges are set for each triangle edge. This makes cooking faster but slow up contact generation.
		*/
		eDISABLE_ACTIVE_EDGES_PRECOMPUTE				=	1 << 4,

		/**
		\brief When set, 32-bit indices will always be created regardless of triangle count.

		\note By default mesh will be created with 16-bit indices for triangle count <= 0xFFFF and 32-bit otherwise.
		*/
		eFORCE_32BIT_INDICES							=	1 << 5
	};
};

typedef PxFlags<PxMeshPreprocessingFlag::Enum,PxU32> PxMeshPreprocessingFlags;

/** \brief Enumeration for mesh cooking hints. */
struct PxMeshCookingHint
{
	enum Enum
	{
		eSIM_PERFORMANCE = 0,		//!< Default value. Favors higher quality hierarchy with higher runtime performance over cooking speed.
		eCOOKING_PERFORMANCE = 1	//!< Enables fast cooking path at the expense of somewhat lower quality hierarchy construction.
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
	\brief Skin width for convexes.

	Specifies the amount to inflate the convex mesh when the PxConvexFlag::eINFLATE_CONVEX is used.

	The value is used for moving planes outward, and beveling sharp edges. This helps the hull generator
	code produce more stable convexes for collision detection. Please note that the resulting hull will
	increase its size, so contact generation may produce noticeable separation between shapes. The separation
	distance can be reduced by decreasing the contactOffset and restOffset.  See the user's manual on
	'Shapes - Tuning Shape Collision Behavior' for details.

	Change the value if the produced hulls are too thin or improper for your usage. Increasing the value
	too much will result in incorrect hull size and a large separation between shapes.

	<b>Default value:</b> 0.025f*PxTolerancesScale.length

	<b>Range:</b> (0.0f, PX_MAX_F32)
	*/
	float		skinWidth;

	/**
	\brief Zero-size area epsilon used in convex hull computation.

	If the area of a triangle of the hull is below this value, the triangle will be rejected. This test
	is done only if PxConvexFlag::eCHECK_ZERO_AREA_TRIANGLES is used.

	@see PxConvexFlag::eCHECK_ZERO_AREA_TRIANGLES

	<b>Default value:</b> 0.06f*PxTolerancesScale.length*PxTolerancesScale.length

	<b>Range:</b> (0.0f, PX_MAX_F32)
	*/
	float		areaTestEpsilon;

	/**
	\brief When true, the face remap table is not created.  This saves a significant amount of memory, but the SDK will
		not be able to provide the remap information for internal mesh triangles returned by collisions, 
		sweeps or raycasts hits.

	<b>Default value:</b> false
	*/
	bool		suppressTriangleMeshRemapTable;

	/**
	\brief When true, the triangle adjacency information is created. You can get the adjacency triangles
	for a given triangle from getTriangle.

	<b>Default value:</b> false
	*/
	bool		buildTriangleAdjacencies;

	/**
	\brief Tolerance scale is used to check if cooked triangles are not too huge. This check will help with simulation stability.

	\note The PxTolerancesScale values have to match the values used when creating a PxPhysics or PxScene instance.

	@see PxTolerancesScale
	*/
	PxTolerancesScale scale;

	/**
	\brief Mesh pre-processing parameters. Used to control options like whether the mesh cooking performs vertex welding before cooking.

	<b>Default value:</b> 0
	*/
	PxMeshPreprocessingFlags	meshPreprocessParams;

	/**
	\brief Mesh cooking hint. Used to specify mesh hierarchy construction preference.

	<b>Default value:</b> PxMeshCookingHint::eSIM_PERFORMANCE
	*/
	PxMeshCookingHint::Enum			meshCookingHint;

	/**
	\brief Mesh weld tolerance. If mesh welding is enabled, this controls the distance at which vertices are welded.
	If mesh welding is not enabled, this value defines the acceptance distance for mesh validation. Provided no two vertices are within this distance, the mesh is considered to be
	clean. If not, a warning will be emitted. Having a clean, welded mesh is required to achieve the best possible performance.

	The default vertex welding uses a snap-to-grid approach. This approach effectively truncates each vertex to integer values using meshWeldTolerance.
	Once these snapped vertices are produced, all vertices that snap to a given vertex on the grid are remapped to reference a single vertex. Following this,
	all triangles' indices are remapped to reference this subset of clean vertices. It should be noted that	the vertices that we do not alter the
	position of the vertices; the snap-to-grid is only performed to identify nearby vertices.

	The mesh validation approach also uses the same snap-to-grid approach to identify nearby vertices. If more than one vertex snaps to a given grid coordinate,
	we ensure that the distance between the vertices is at least meshWeldTolerance. If this is not the case, a warning is emitted.

	<b>Default value:</b> 0.0
	*/
	PxReal		meshWeldTolerance;

	/**
	\brief Controls the trade-off between mesh size and runtime performance.

	Using a value of 1.0 will produce a larger cooked mesh with generally higher runtime performance,
	using 0.0 will produce a smaller cooked mesh, with generally lower runtime performance.

	Values outside of [0,1] range will be clamped and cause a warning when any mesh gets cooked.

	<b>Default value:</b> 0.55
	<b>Range:</b> [0.0f, 1.0f]
	*/
	PxF32 meshSizePerformanceTradeOff;

	PxCookingParams(const PxTolerancesScale& sc):
		skinWidth(0.025f*sc.length),
		areaTestEpsilon(0.06f*sc.length*sc.length),
		suppressTriangleMeshRemapTable(false),
		buildTriangleAdjacencies(false),
		scale(sc),
		meshPreprocessParams(0),
		meshCookingHint(PxMeshCookingHint::eSIM_PERFORMANCE),
		meshWeldTolerance(0.f),
		meshSizePerformanceTradeOff(0.55f)
	{
#if defined(PX_X86) || defined(PX_X64)
		targetPlatform = PxPlatform::ePC;
#elif defined(PX_X360)
		targetPlatform = PxPlatform::eXENON;
#elif defined(PX_PS3)
		targetPlatform = PxPlatform::ePLAYSTATION3;
#elif defined(PX_ARM) || defined(PX_A64)
		targetPlatform = PxPlatform::eARM;
#elif defined(PX_WIIU)
		targetPlatform = PxPlatform::eWIIU;
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

	This function should be called to cleanly shut down the Cooking library before application exit.

	\note This function is required to be called to release foundation usage.

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

	@see cookConvexMesh() setParams() PxPhysics.createTriangleMesh()
	*/
	virtual bool  cookTriangleMesh(const PxTriangleMeshDesc& desc, PxOutputStream& stream) = 0;

	/**
	\brief Cooks and creates a triangle mesh and inserts it into PxPhysics.

	\param[in] desc The triangle mesh descriptor to read the mesh from.
	\param[in] insertionCallback The insertion interface from PxPhysics.
	\return PxTriangleMesh pointer on success	

	@see cookConvexMesh() setParams() PxPhysics.createTriangleMesh() PxPhysicsInsertionCallback
	*/
	virtual PxTriangleMesh*    createTriangleMesh(const PxTriangleMeshDesc& desc, PxPhysicsInsertionCallback& insertionCallback) = 0;

	/**
	\brief Verifies if the triangle mesh is valid. Prints an error message for each inconsistency found.

	The following conditions are true for a valid triangle mesh:
	1. There are no duplicate vertices (within specified vertexWeldTolerance. See PxCookingParams::meshWeldTolerance)
    2. There are no large triangles (within specified PxTolerancesScale.)

	\param[in] desc The triangle mesh descriptor to read the mesh from.

	\return true if all the validity conditions hold, false otherwise.

	@see cookTriangleMesh()
	*/
	virtual bool  validateTriangleMesh(const PxTriangleMeshDesc& desc) = 0;

	/**
	\brief Cooks a convex mesh. The results are written to the stream.

	To create a triangle mesh object it is necessary to first 'cook' the mesh data into
	a form which allows the SDK to perform efficient collision detection.

	cookConvexMesh() allows a mesh description to be cooked into a binary stream
	suitable for loading and performing collision detection at runtime.

	Example

	\include PxCookConvexMesh_Example.cpp

	\note The number of vertices and the number of convex polygons in a cooked convex mesh is limited to 256.
	\note If those limits are exceeded in either the user-provided data or the final cooked mesh, an error is reported.
	\note If the number of polygons exceed, using the #PxConvexFlag::eINFLATE_CONVEX can help you to obtain a valid convex.

	\param[in] desc The convex mesh descriptor to read the mesh from.
	\param[in] stream User stream to output the cooked data.
	\param[out] condition Result from convex mesh cooking.
	\return true on success

	@see cookTriangleMesh() setParams() PxConvexMeshCookingResult::Enum
	*/
	virtual bool  cookConvexMesh(const PxConvexMeshDesc& desc, PxOutputStream& stream, PxConvexMeshCookingResult::Enum* condition = NULL) = 0;	

	/**
	\brief Computed hull polygons from given vertices and triangles. Polygons are needed for PxConvexMeshDesc rather than triangles.

	Please note that the resulting polygons may have different number of vertices. Some vertices may be removed. 
	The output vertices, indices and polygons must be used to construct a hull.

	The provided PxAllocatorCallback does allocate the out array's. It is the user responsibility to deallocated those
	array's.

	\param[in] mesh Simple triangle mesh containing vertices and triangles used to compute polygons.
	\param[in] inCallback Memory allocator for out array allocations.
	\param[out] nbVerts Number of vertices used by polygons.
	\param[out] vertices Vertices array used by polygons.
	\param[out] nbIndices Number of indices used by polygons.
	\param[out] indices Indices array used by polygons.
	\param[out] nbPolygons Number of created polygons.
	\param[out] hullPolygons Polygons array.
	\return true on success

	@see cookConvexMesh() PxConvexFlags PxConvexMeshDesc PxSimpleTriangleMesh
	*/
	virtual bool  computeHullPolygons(const PxSimpleTriangleMesh& mesh, PxAllocatorCallback& inCallback, PxU32& nbVerts, PxVec3*& vertices,
											PxU32& nbIndices, PxU32*& indices, PxU32& nbPolygons, PxHullPolygon*& hullPolygons) = 0;

	/**
	\brief Cooks a heightfield. The results are written to the stream.

	To create a heightfield object there is an option to precompute some of calculations done while loading the heightfield data.

	cookHeightField() allows a heightfield description to be cooked into a binary stream
	suitable for loading and performing collision detection at runtime.

	\param[in] desc The heightfield descriptor to read the HF from.
	\param[in] stream User stream to output the cooked data.
	\return true on success

	@see PxPhysics.createHeightField()
	*/
	virtual bool  cookHeightField(const PxHeightFieldDesc& desc, PxOutputStream& stream) = 0;

		/**
	\brief Cooks and creates a heightfield mesh and inserts it into PxPhysics.

	\param[in] desc The heightfield descriptor to read the HF from.
	\param[in] insertionCallback The insertion interface from PxPhysics.
	\return PxHeightField pointer on success

	@see cookConvexMesh() setParams() PxPhysics.createTriangleMesh() PxPhysicsInsertionCallback
	*/
	virtual PxHeightField*    createHeightField(const PxHeightFieldDesc& desc, PxPhysicsInsertionCallback& insertionCallback) = 0;


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
