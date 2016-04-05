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


#ifndef PX_STRING_TABLE_H
#define PX_STRING_TABLE_H


/** \addtogroup physics
@{
*/

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
 *	\brief a table to manage strings.  Strings allocated through this object are expected to be owned by this object.
 */
class PxStringTable
{
protected:
	virtual ~PxStringTable(){}
public:
	/**
	 *	\brief Allocate a new string.
	 *
	 *	\param[in] inSrc Source string, null terminated or null.
	 *	
	 *	\return *Always* a valid null terminated string.  "" is returned if "" or null is passed in.
	 */
	virtual const char* allocateStr( const char* inSrc ) = 0;

	/**
	 *	Release the string table and all the strings associated with it.
	 */
	virtual void release() = 0;
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
