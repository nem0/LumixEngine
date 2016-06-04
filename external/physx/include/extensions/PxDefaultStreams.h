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


#ifndef PX_PHYSICS_EXTENSIONS_DEFAULT_STREAMS_H
#define PX_PHYSICS_EXTENSIONS_DEFAULT_STREAMS_H
/** \addtogroup extensions
  @{
*/

#include <stdio.h>
#include "common/PxPhysXCommonConfig.h"
#include "foundation/PxIO.h"
#include "foundation/PxFoundation.h"

#ifndef PX_WIIU
	typedef FILE* PxFileHandle;
#else
	#include "extensions/wiiu/PxWiiUFileHandle.h"
#endif

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/** 
\brief default implementation of a memory write stream

@see PxOutputStream
*/

class PxDefaultMemoryOutputStream: public PxOutputStream
{
public:
						PxDefaultMemoryOutputStream(PxAllocatorCallback &allocator = PxGetFoundation().getAllocatorCallback());
	virtual				~PxDefaultMemoryOutputStream();

	virtual	PxU32		write(const void* src, PxU32 count);

	virtual	PxU32		getSize()	const	{	return mSize; }
	virtual	PxU8*		getData()	const	{	return mData; }

private:
		PxDefaultMemoryOutputStream(const PxDefaultMemoryOutputStream&);
		PxDefaultMemoryOutputStream& operator=(const PxDefaultMemoryOutputStream&);

		PxAllocatorCallback&	mAllocator;
		PxU8*					mData;
		PxU32					mSize;
		PxU32					mCapacity;
};

/** 
\brief default implementation of a memory read stream

@see PxInputData
*/
	
class PxDefaultMemoryInputData: public PxInputData
{
public:
						PxDefaultMemoryInputData(PxU8* data, PxU32 length);

	virtual		PxU32	read(void* dest, PxU32 count);
	virtual		PxU32	getLength() const;
	virtual		void	seek(PxU32 pos);
	virtual		PxU32	tell() const;

private:
		PxU32		mSize;
		const PxU8*	mData;
		PxU32		mPos;
};



/** 
\brief default implementation of a file write stream

@see PxOutputStream
*/

class PxDefaultFileOutputStream: public PxOutputStream
{
public:
						PxDefaultFileOutputStream(const char* name);
	virtual				~PxDefaultFileOutputStream();

	virtual		PxU32	write(const void* src, PxU32 count);
	virtual		bool	isValid();
private:
		PxFileHandle	mFile;
};


/** 
\brief default implementation of a file read stream

@see PxInputData
*/

class PxDefaultFileInputData: public PxInputData
{
public:
						PxDefaultFileInputData(const char* name);
	virtual				~PxDefaultFileInputData();

	virtual		PxU32	read(void* dest, PxU32 count);
	virtual		void	seek(PxU32 pos);
	virtual		PxU32	tell() const;
	virtual		PxU32	getLength() const;
				
				bool	isValid() const;
private:
		PxFileHandle	mFile;
		PxU32			mLength;
};

#ifndef PX_DOXYGEN
}
#endif

/** @} */

#endif

