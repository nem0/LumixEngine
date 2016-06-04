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

#ifndef PX_PHYSICS_CCT_OBSTACLES
#define PX_PHYSICS_CCT_OBSTACLES
/** \addtogroup character
  @{
*/

#include "characterkinematic/PxCharacter.h"
#include "characterkinematic/PxExtended.h"
#include "geometry/PxGeometry.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	class PxControllerManager;

	#define INVALID_OBSTACLE_HANDLE	0xffffffff

	/**
	\brief Base class for obstacles.

	@see PxBoxObstacle PxCapsuleObstacle PxObstacleContext
	*/
	class PxObstacle
	{
		protected:
												PxObstacle() :
													mType		(PxGeometryType::eINVALID),
													mUserData	(NULL),
													mPos		(0.0, 0.0, 0.0),
													mRot		(PxQuat(PxIdentity))
												{}
												~PxObstacle()					{}

						PxGeometryType::Enum	mType; 
		public:

		PX_FORCE_INLINE	PxGeometryType::Enum	getType()	const	{ return mType;	}

						void*					mUserData;
						PxExtendedVec3			mPos;
						PxQuat					mRot;
	};

	/**
	\brief A box obstacle.

	@see PxObstacle PxCapsuleObstacle PxObstacleContext
	*/
	class PxBoxObstacle : public PxObstacle
	{
		public:
												PxBoxObstacle() :
													mHalfExtents(0.0f)
												{ mType = PxGeometryType::eBOX;		 }
												~PxBoxObstacle()		{}

						PxVec3					mHalfExtents;
	};

	/**
	\brief A capsule obstacle.

	@see PxBoxObstacle PxObstacle PxObstacleContext
	*/
	class PxCapsuleObstacle : public PxObstacle
	{
		public:
												PxCapsuleObstacle() :
													mHalfHeight	(0.0f),
													mRadius		(0.0f)
												{ mType = PxGeometryType::eCAPSULE;	 }
												~PxCapsuleObstacle()								{}

						PxReal					mHalfHeight;
						PxReal					mRadius;
	};

	typedef PxU32	ObstacleHandle;

	/**
	\brief Context class for obstacles.

	An obstacle context class contains and manages a set of user-defined obstacles.

	@see PxBoxObstacle PxCapsuleObstacle PxObstacle
	*/
	class PxObstacleContext
	{
		public:
									PxObstacleContext()		{}
		virtual						~PxObstacleContext()	{}

		/**
		\brief Releases the context.
		*/
		virtual	void				release()															= 0;

		/**
		\brief Retrieves the controller manager associated with this context.

		\return The associated controller manager
		*/
		virtual PxControllerManager&	getControllerManager() const									= 0;

		/**
		\brief Adds an obstacle to the context.

		\param	[in]	obstacle	Obstacle data for the new obstacle. The data gets copied.

		\return Handle for newly-added obstacle
		*/
		virtual	ObstacleHandle		addObstacle(const PxObstacle& obstacle)								= 0;

		/**
		\brief Removes an obstacle from the context.

		\param	[in]	handle	Handle for the obstacle object that needs to be removed.

		\return True if success
		*/
		virtual	bool				removeObstacle(ObstacleHandle handle)								= 0;

		/**
		\brief Updates data for an existing obstacle.

		\param	[in]	handle		Handle for the obstacle object that needs to be updated.
		\param	[in]	obstacle	New obstacle data

		\return True if success
		*/
		virtual	bool				updateObstacle(ObstacleHandle handle, const PxObstacle& obstacle)	= 0;

		/**
		\brief Retrieves number of obstacles in the context.

		\return Number of obstacles in the context
		*/
		virtual	PxU32				getNbObstacles()											const	= 0;

		/**
		\brief Retrieves desired obstacle.

		\param	[in]	i			Obstacle index

		\return Desired obstacle
		*/
		virtual	const PxObstacle*	getObstacle(PxU32 i)										const	= 0;

		/**
		\brief Retrieves desired obstacle by given handle.

		\param	[in]	handle			Obstacle handle

		\return Desired obstacle
		*/
		virtual	const PxObstacle*	getObstacleByHandle(ObstacleHandle handle)					const	= 0;
	};

#ifndef PX_DOXYGEN
}
#endif

/** @} */
#endif
