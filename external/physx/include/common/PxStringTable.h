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
