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



#ifndef PVD_BITS_H
#define PVD_BITS_H
#include "physxvisualdebuggersdk/PvdObjectModelBaseTypes.h"

namespace physx { namespace debugger {
	
//Marshallers cannot assume src is aligned, but they can assume dest is aligned.
typedef void (*TSingleMarshaller)( const PxU8* src, PxU8* dest );
typedef void (*TBlockMarshaller)( const PxU8* src, PxU8* dest, PxU32 numItems );

template<PxU8 ByteCount>
static inline void doSwapBytes( PxU8* __restrict inData )
{
	for ( PxU32 idx = 0; idx < ByteCount/2; ++idx )
	{
		PxU32 endIdx = ByteCount-idx-1;
		PxU8 theTemp = inData[idx];
		inData[idx] = inData[endIdx];
		inData[endIdx] = theTemp;
	}
}

template<PxU8 ByteCount>
static inline void doSwapBytes( PxU8* __restrict inData, PxU32 itemCount )
{
	PxU8* end = inData + itemCount * ByteCount;
	for( ; inData < end; inData += ByteCount )
		doSwapBytes<ByteCount>( inData );
}

static inline void swapBytes( PxU8* __restrict dataPtr, PxU32 numBytes, PxU32 itemWidth )
{
	PxU32 numItems = numBytes / itemWidth;
	switch( itemWidth )
	{
		case 1: break;
		case 2: doSwapBytes<2>( dataPtr, numItems ); break;
		case 4: doSwapBytes<4>( dataPtr, numItems ); break;
		case 8: doSwapBytes<8>( dataPtr, numItems ); break;
		case 16: doSwapBytes<16>( dataPtr, numItems ); break;
		default: PX_ASSERT( false ); break;
	}
}

template<PxU8 TByteCount, bool TShouldSwap>
struct PvdByteSwapper
{
	void swapBytes( PxU8* __restrict inData )
	{
		doSwapBytes<TByteCount>( inData );
	};
	void swapBytes( PxU8* __restrict inData, PxU32 itemCount )
	{
		doSwapBytes<TByteCount>( inData, itemCount );
	}
	void swapBytes( PxU8* __restrict dataPtr, PxU32 numBytes, PxU32 itemWidth )
	{
		physx::debugger::swapBytes( dataPtr, numBytes, itemWidth );
	}
};

struct PvdNullSwapper
{
	
	void swapBytes( PxU8* __restrict )
	{
	};
	void swapBytes( PxU8* __restrict, PxU32)
	{
	}
	void swapBytes( PxU8* __restrict, PxU32, PxU32 )
	{
	}
};
//Anything that doesn't need swapping gets the null swapper
template<PxU8 TByteCount> struct PvdByteSwapper<TByteCount,false> : public PvdNullSwapper {};
//A 1 byte byte swapper can't really do anything.
template<> struct PvdByteSwapper<1,true> : public PvdNullSwapper {};


static inline void swapBytes( PxU8&) {}
static inline void swapBytes( PxI8&) {}
static inline void swapBytes( PxU16& inData) { doSwapBytes<2>(reinterpret_cast<PxU8*>( &inData ) ); }
static inline void swapBytes( PxI16& inData) { doSwapBytes<2>(reinterpret_cast<PxU8*>( &inData ) ); }
static inline void swapBytes( PxU32& inData) { doSwapBytes<4>(reinterpret_cast<PxU8*>( &inData ) ); }
static inline void swapBytes( PxI32& inData) { doSwapBytes<4>(reinterpret_cast<PxU8*>( &inData ) ); }
static inline void swapBytes( PxF32& inData) { doSwapBytes<4>(reinterpret_cast<PxU8*>( &inData ) ); }
static inline void swapBytes( PxU64& inData) { doSwapBytes<8>(reinterpret_cast<PxU8*>( &inData ) ); }
static inline void swapBytes( PxI64& inData) { doSwapBytes<8>(reinterpret_cast<PxU8*>( &inData ) ); }
static inline void swapBytes( PxF64& inData) { doSwapBytes<8>(reinterpret_cast<PxU8*>( &inData ) ); }

static inline bool checkLength( const PxU8* inStart, const PxU8* inStop, PxU32 inLength )
{
	return static_cast<PxU32>(inStop - inStart) >= inLength;
}

}}

#endif