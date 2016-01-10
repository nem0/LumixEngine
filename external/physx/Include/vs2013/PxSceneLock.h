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


#ifndef PX_PHYSICS_NX_SCENELOCK
#define PX_PHYSICS_NX_SCENELOCK
/** \addtogroup physics
@{
*/

#include "PxPhysXConfig.h"
#include "PxScene.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief RAII wrapper for the PxScene read lock.

Use this class as follows to lock the scene for reading by the current thread 
for the duration of the enclosing scope:

	PxSceneReadLock lock(sceneRef);

\see PxScene::lockRead(), PxScene::unlockRead(), PxSceneFlag::eREQUIRE_RW_LOCK
*/
class PxSceneReadLock
{
	PxSceneReadLock(const PxSceneReadLock&);
	PxSceneReadLock& operator=(const PxSceneReadLock&);

public:
	
	/**
	\brief Constructor
	\param scene The scene to lock for reading
	\param file Optional string for debugging purposes
	\param line Optional line number for debugging purposes
	*/
	PxSceneReadLock(PxScene& scene, const char* file=NULL, PxU32 line=0)
		: mScene(scene)
	{
		mScene.lockRead(file, line);
	}

	~PxSceneReadLock()
	{
		mScene.unlockRead();
	}

private:

	PxScene& mScene;
};

/**
\brief RAII wrapper for the PxScene write lock.

Use this class as follows to lock the scene for writing by the current thread 
for the duration of the enclosing scope:

	PxSceneWriteLock lock(sceneRef);

\see PxScene::lockWrite(), PxScene::unlockWrite(), PxSceneFlag::eREQUIRE_RW_LOCK
*/
class PxSceneWriteLock
{
	PxSceneWriteLock(const PxSceneWriteLock&);
	PxSceneWriteLock& operator=(const PxSceneWriteLock&);

public:

	/**
	\brief Constructor
	\param scene The scene to lock for writing
	\param file Optional string for debugging purposes
	\param line Optional line number for debugging purposes
	*/
	PxSceneWriteLock(PxScene& scene, const char* file=NULL, PxU32 line=0)
		: mScene(scene)
	{
		mScene.lockWrite(file, line);
	}

	~PxSceneWriteLock()
	{
		mScene.unlockWrite();
	}

private:

	PxScene& mScene;
};


#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
