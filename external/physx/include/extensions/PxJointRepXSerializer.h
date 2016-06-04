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
#ifndef PX_JOINT_REPX_SERIALIZER_H
#define PX_JOINT_REPX_SERIALIZER_H
/** \addtogroup RepXSerializers
  @{
*/

#include "extensions/PxRepXSimpleType.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif
	
	template<typename TLiveType>
	struct RepXSerializerImpl;
	
	class XmlReader;
	class XmlMemoryAllocator;
	class XmlWriter;
	class MemoryBuffer;
			
	template<typename TJointType>
	struct PxJointRepXSerializer : RepXSerializerImpl<TJointType>
	{
		PxJointRepXSerializer( PxAllocatorCallback& inAllocator ) : RepXSerializerImpl<TJointType>( inAllocator ) {}
		virtual PxRepXObject fileToObject( XmlReader& inReader, XmlMemoryAllocator& inAllocator, PxRepXInstantiationArgs& inArgs, PxCollection* inCollection);
		virtual void objectToFileImpl( const TJointType* inObj, PxCollection* inCollection, XmlWriter& inWriter, MemoryBuffer& inTempBuffer, PxRepXInstantiationArgs&);
		virtual TJointType* allocateObject( PxRepXInstantiationArgs& ) { return NULL; }
	};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
