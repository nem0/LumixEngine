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


#ifndef PX_CORE_UTILTY_TYPES_H
#define PX_CORE_UTILTY_TYPES_H
/** \addtogroup common
@{
*/

#include "foundation/PxAssert.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/** 
 *	An array of pointers.  Used for at least materials and shapes
 *	in the descriptor hierarchy.
 */
template<typename TDataType>
class PxPtrArray
{
	PxU32 count;
	TDataType*const* items;
	TDataType* singleItem;
public:

	PX_INLINE PxPtrArray()
		: count ( 0 )
		, items ( NULL )
		, singleItem ( NULL )
	{
	}
	PX_INLINE PxPtrArray( const PxPtrArray<TDataType>& inOther )
	{
		(*this) = inOther;
	}

	PX_INLINE PxPtrArray<TDataType>& operator=( const PxPtrArray<TDataType>& inOther )
	{
		//This is harder to get right than it would seem
		//The problem is if you have a vector of these items
		//and they are being copied around.  Then the pointer to
		//a previous item's single item is probably bad.  Thus
		//you need to reconstruct the single item chain.
		//CN
		count = inOther.count;
		if ( count == 1 )
		{
			singleItem = inOther.items[0];
			items = &singleItem;
		}
		else
		{
			singleItem = NULL;
			items = inOther.items;
		}
		return *this;
	}

	/**
	 * set the contents to be a list of ptr-to-ptr-to-items.
	 */
	PX_INLINE void set(TDataType*const* items_, PxU32 count_)
	{
		items = items_;
		count = count_;
	}

	/**
	\brief set a single item as the content of the reference array
	*/
	PX_INLINE void setSingle(TDataType* item_)
	{
		singleItem = item_;
		items = &singleItem;
		count = 1;
	}

	PX_INLINE bool isValid() const 
	{ 
		if ( count ) 
			return items != NULL; 
		return items == NULL;
	}
	
	PX_INLINE PxU32 getCount() const { return count; }
	PX_INLINE TDataType*const* getItems() const { return items; }

	PX_INLINE TDataType* operator[]( PxU32 idx ) const { return items[idx]; }
};

struct PxStridedData
{
	/**
	\brief The offset in bytes between consecutive samples in the data.

	<b>Default:</b> 0
	*/
	PxU32 stride;
	const void* data;

	PxStridedData() : stride( 0 ), data( NULL ) {}

	template<typename TDataType>
	PX_INLINE const TDataType& at( PxU32 idx ) const
	{
		PxU32 theStride( stride );
		if ( theStride == 0 )
			theStride = sizeof( TDataType );
		PxU32 offset( theStride * idx );
		return *(reinterpret_cast<const TDataType*>( reinterpret_cast< const PxU8* >( data ) + offset ));
	}
};

template<typename TDataType>
struct PxTypedStridedData
{
	PxU32 stride;
	const TDataType* data;

	PxTypedStridedData()
		: stride( 0 )
		, data( NULL )
	{
	}

};

struct PxBoundedData : public PxStridedData
{
	PxU32 count;
	PxBoundedData() : count( 0 ) {}
};

template<PxU8 TNumBytes>
struct PxPadding
{
	PxU8 mPadding[TNumBytes];
	PxPadding()
	{
		for ( PxU8 idx =0; idx < TNumBytes; ++idx )
			mPadding[idx] = 0;
	}
};


template <PxU32 NUM_ELEMENTS> class PxFixedSizeLookupTable
{
public:
	
	PxFixedSizeLookupTable() 
		: mNumDataPairs(0)
	{
	}

	PxFixedSizeLookupTable(const PxReal* dataPairs, const PxU32 numDataPairs)
	{
		memcpy(mDataPairs,dataPairs,sizeof(PxReal)*2*numDataPairs);
		mNumDataPairs=numDataPairs;
	}

	PxFixedSizeLookupTable(const PxFixedSizeLookupTable& src)
	{
		memcpy(mDataPairs,src.mDataPairs,sizeof(PxReal)*2*src.mNumDataPairs);
		mNumDataPairs=src.mNumDataPairs;
	}

	~PxFixedSizeLookupTable()
	{
	}

	PxFixedSizeLookupTable& operator=(const PxFixedSizeLookupTable& src)
	{
		memcpy(mDataPairs,src.mDataPairs,sizeof(PxReal)*2*src.mNumDataPairs);
		mNumDataPairs=src.mNumDataPairs;
		return *this;
	}

	PX_FORCE_INLINE void addPair(const PxReal x, const PxReal y)
	{
		PX_ASSERT(mNumDataPairs<NUM_ELEMENTS);
		mDataPairs[2*mNumDataPairs+0]=x;
		mDataPairs[2*mNumDataPairs+1]=y;
		mNumDataPairs++;
	}

	PX_FORCE_INLINE PxReal getYVal(const PxReal x) const
	{
		if(0==mNumDataPairs)
		{
			PX_ASSERT(false);
			return 0;
		}

		if(1==mNumDataPairs || x<getX(0))
		{
			return getY(0);
		}

		PxReal x0=getX(0);
		PxReal y0=getY(0);

		for(PxU32 i=1;i<mNumDataPairs;i++)
		{
			const PxReal x1=getX(i);
			const PxReal y1=getY(i);

			if((x>=x0)&&(x<x1))
			{
				return (y0+(y1-y0)*(x-x0)/(x1-x0));
			}

			x0=x1;
			y0=y1;
		}

		PX_ASSERT(x>=getX(mNumDataPairs-1));
		return getY(mNumDataPairs-1);
	}

	PxU32 getNumDataPairs() const {return mNumDataPairs;}

private:

	PxReal mDataPairs[2*NUM_ELEMENTS];
	PxU32 mNumDataPairs;
	PxU32 mPad[3];

	PX_FORCE_INLINE PxReal getX(const PxU32 i) const
	{
		return mDataPairs[2*i];
	}
	PX_FORCE_INLINE PxReal getY(const PxU32 i) const
	{
		return mDataPairs[2*i+1];
	}
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
