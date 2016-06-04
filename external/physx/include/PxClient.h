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


#ifndef PX_PHYSICS_NX_CLIENT
#define PX_PHYSICS_NX_CLIENT

#include "foundation/PxFlags.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief An ID to identify different clients for multiclient support.

@see PxScene::createClient() 
*/
typedef PxU8 PxClientID;

/**
\brief The predefined default PxClientID value.

@see PxClientID PxScene::createClient() 
*/
static const PxClientID PX_DEFAULT_CLIENT = 0;

/**
\brief The maximum number of clients we support.

@see PxClientID PxScene::createClient() 
*/
static const PxClientID PX_MAX_CLIENTS = 128;

/**
\brief Behavior bit flags for simulation clients.

@see PxClientBehaviorFlags PxScene::setClientBehaviorFlags() 
*/
struct PxClientBehaviorFlag
{ 
	enum Enum 
	{
		/**
		\brief Report actors belonging to other clients to the trigger callback of this client.

		@see PxSimulationEventCallback::onTrigger()
		*/
		eREPORT_FOREIGN_OBJECTS_TO_TRIGGER_NOTIFY			= (1<<0),
		/**
		\brief Report actors belonging to other clients to the contact callback of this client.

		@see PxSimulationEventCallback::onContact()
		*/
		eREPORT_FOREIGN_OBJECTS_TO_CONTACT_NOTIFY			= (1<<1),
		/**
		\brief Report actors belonging to other clients to the constraint break callback of this client.

		@see PxSimulationEventCallback::onConstraintBreak()
		*/
		eREPORT_FOREIGN_OBJECTS_TO_CONSTRAINT_BREAK_NOTIFY	= (1<<2),
		/**
		\brief Report actors belonging to other clients to scene queries of this client.
		*/
		eREPORT_FOREIGN_OBJECTS_TO_SCENE_QUERY				= (1<<3)
	};
};

/**
\brief Bitfield that contains a set of raised flags defined in PxClientBehaviorFlag.

@see PxClientBehaviorFlag PxScene::setClientBehaviorFlags() 
*/
typedef PxFlags<PxClientBehaviorFlag::Enum, PxU8> PxClientBehaviorFlags;
PX_FLAGS_OPERATORS(PxClientBehaviorFlag::Enum, PxU8)


/**
\brief Multiclient behavior bit flags for actors.

@see PxActorClientBehaviorFlags PxActor::setClientBehaviorFlags()
*/
struct PxActorClientBehaviorFlag
{ 
	enum Enum
	{
		/**
		\brief Report this actor to trigger callbacks of other clients.

		@see PxSimulationEventCallback::onTrigger()
		*/
		eREPORT_TO_FOREIGN_CLIENTS_TRIGGER_NOTIFY			= (1<<0),
		/**
		\brief Report this actor to contact callbacks of other clients.

		@see PxSimulationEventCallback::onContact()
		*/
		eREPORT_TO_FOREIGN_CLIENTS_CONTACT_NOTIFY			= (1<<1),
		/**
		\brief Report this actor to constraint break callbacks of other clients.

		@see PxSimulationEventCallback::onConstraintBreak()
		*/
		eREPORT_TO_FOREIGN_CLIENTS_CONSTRAINT_BREAK_NOTIFY	= (1<<2),
		/**
		\brief Report this actor to scene queries of other clients.
		*/
		eREPORT_TO_FOREIGN_CLIENTS_SCENE_QUERY				= (1<<3)
	};
};

/**
\brief Bitfield that contains a set of raised flags defined in PxActorClientBehaviorFlag.

@see PxActorClientBehaviorFlag PxActor::setClientBehaviorFlags()
*/
typedef PxFlags<PxActorClientBehaviorFlag::Enum, PxU8> PxActorClientBehaviorFlags;
PX_FLAGS_OPERATORS(PxActorClientBehaviorFlag::Enum, PxU8)

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#endif
