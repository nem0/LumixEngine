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


#ifndef PX_FOUNDATION_PX_IO_H
#define PX_FOUNDATION_PX_IO_H

/** \addtogroup common
  @{
*/

#include "common/PxPhysXCommonConfig.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Input stream class for I/O.

The user needs to supply a PxInputStream implementation to a number of methods to allow the SDK to read data. 
*/



class PxInputStream
{
public:

	/**
	\brief read from the stream. The number of bytes read may be less than the number requested.

	\param[in] dest the destination address to which the data will be read
	\param[in] count the number of bytes requested

	\return the number of bytes read from the stream.
	*/

	virtual PxU32 read(void* dest, PxU32 count) = 0;

	virtual ~PxInputStream() {}
};


/**
\brief Input data class for I/O which provides random read access.

The user needs to supply a PxInputData implementation to a number of methods to allow the SDK to read data. 
*/

class PxInputData : public PxInputStream
{
public:

	/**
	\brief return the length of the input data

	\return size in bytes of the input data
	*/

	virtual PxU32	getLength() const			= 0;


	/**
	\brief seek to the given offset from the start of the data. 
	
	\param[in] offset the offset to seek to. 	If greater than the length of the data, this call is equivalent to seek(length);
	*/

	virtual void	seek(PxU32 offset)		= 0;

	/**
	\brief return the current offset from the start of the data
	
	\return the offset to seek to.
	*/

	virtual PxU32	tell() const			= 0;

	virtual ~PxInputData() {}
};

/**
\brief Output stream class for I/O.

The user needs to supply a PxOutputStream implementation to a number of methods to allow the SDK to write data. 
*/

class PxOutputStream
{
public:
	/**
	\brief write to the stream. The number of bytes written may be less than the number sent.

	\param[in] src the destination address from which the data will be written
	\param[in] count the number of bytes to be written

	\return the number of bytes written to the stream by this call.
	*/

	virtual PxU32 write(const void* src, PxU32 count) = 0;

	virtual ~PxOutputStream() {}

};





#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
