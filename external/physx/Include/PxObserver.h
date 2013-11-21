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


#ifndef PX_PHYSICS_NX_OBSERVER
#define PX_PHYSICS_NX_OBSERVER
/** \addtogroup physics
@{
*/

#include "PxPhysX.h"
#include "common/PxSerialFramework.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxObservable;


/**
\brief Observer interface to get notification on object state changes.

@see PxObservable
*/
class PxObserver : public PxSerializable
{
public:
	/**
	\brief Notification if an object the observer registered with gets released

	\note It is not allowed to change the object state in this callback. Furthermore, when reading from the object it is the user's responsibility to make sure 
	that no other thread is writing at the same time to the object (this includes the simulation itself, i.e., #PxScene::fetchResults() must not get called at the same time).

	\param[out] observable The object that's going to be released

	@see PxObservable
	*/
	virtual void onRelease(const PxObservable& observable) = 0;

protected:
	PxObserver(PxRefResolver& v) : PxSerializable(v) {}
	PxObserver() {}
	virtual ~PxObserver() {}
	virtual		bool			isKindOf(const char* name)	const		{	return !strcmp("PxObserver", name) || PxSerializable::isKindOf(name); }
};


/**
\brief Identifier for the different observable object types.

@see PxObservable
*/
struct PxObservableType
{
	enum Enum
	{
		/**
		\brief A PxActor object
		@see PxActor
		*/
		eActor = 0,
	};
};


/**
\brief Observable interface for classes which can send out state change notifications.

@see PxObserver
*/
class PxObservable
{
public:
	/**
	\brief Return the type of the observable object.

	\return Type of observable object

	@see PxObservableType
	*/
	virtual PxObservableType::Enum getObservableType() const = 0;

	/**
	\brief Register an observer.

	\param[in] observer Observer object to send notifications to

	@see PxObserver
	*/
	virtual void registerObserver(PxObserver& observer) = 0;

	/**
	\brief Unregister an observer.

	\param[in] observer Observer object to send notifications to

	@see PxObserver
	*/
	virtual void unregisterObserver(PxObserver& observer) = 0;

	/**
	\brief Return the number of registered observers.

	\return Number of registered observers

	@see getObservers() PxObserver
	*/
	virtual PxU32 getNbObservers() const = 0;

	/**
	\brief Retrieve an array of all the registered observers.

	\param[out] userBuffer The buffer to receive observer pointers.
	\param[in] bufferSize Size of provided user buffer.
	\return Number of observers written to the buffer.

	@see getNbObservers() PxObserver
	*/
	virtual PxU32 getObservers(PxObserver** userBuffer, PxU32 bufferSize) const = 0;

protected:
	virtual ~PxObservable() {}
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
