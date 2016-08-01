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


#ifndef PX_FOUNDATION_PX_FLAGS_H
#define PX_FOUNDATION_PX_FLAGS_H

/** \addtogroup foundation
  @{
*/

#include "foundation/Px.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif
	/**
	\brief Container for bitfield flag variables associated with a specific enum type.
	
	This allows for type safe manipulation for bitfields.
	
	<h3>Example</h3>
		// enum that defines each bit...
		struct MyEnum
		{
			enum Enum
			{
				eMAN  = 1,
				eBEAR = 2,
				ePIG  = 4,
			};
		};
		
		// implements some convenient global operators.
		PX_FLAGS_OPERATORS(MyEnum::Enum, PxU8);
		
		PxFlags<MyEnum::Enum, PxU8> myFlags;
		myFlags |= MyEnum::eMAN;
		myFlags |= MyEnum::eBEAR | MyEnum::ePIG;
		if(myFlags & MyEnum::eBEAR)
		{
			doSomething();
		}
	*/

	template<typename enumtype, typename storagetype=PxU32>
	class PxFlags
	{
	public:
		typedef storagetype InternalType;

		PX_INLINE	explicit	PxFlags(const PxEMPTY&)	{}
		PX_INLINE				PxFlags(void);
		PX_INLINE				PxFlags(enumtype e);
		PX_INLINE				PxFlags(const PxFlags<enumtype,storagetype> &f);
		PX_INLINE	explicit	PxFlags(storagetype b);		
		
		PX_INLINE bool							 isSet     (enumtype e) const;
		PX_INLINE PxFlags<enumtype,storagetype> &set       (enumtype e);
		PX_INLINE bool                           operator==(enumtype e) const;
		PX_INLINE bool                           operator==(const PxFlags<enumtype,storagetype> &f) const;
		PX_INLINE bool                           operator==(bool b) const;
		PX_INLINE bool                           operator!=(enumtype e) const;
		PX_INLINE bool                           operator!=(const PxFlags<enumtype,storagetype> &f) const;

		PX_INLINE PxFlags<enumtype,storagetype> &operator =(enumtype e);
		PX_INLINE PxFlags<enumtype,storagetype> &operator =(const PxFlags<enumtype,storagetype> &f);
		
		PX_INLINE PxFlags<enumtype,storagetype> &operator|=(enumtype e);
		PX_INLINE PxFlags<enumtype,storagetype> &operator|=(const PxFlags<enumtype,storagetype> &f);
		PX_INLINE PxFlags<enumtype,storagetype>  operator| (enumtype e) const;
		PX_INLINE PxFlags<enumtype,storagetype>  operator| (const PxFlags<enumtype,storagetype> &f) const;
		
		PX_INLINE PxFlags<enumtype,storagetype> &operator&=(enumtype e);
		PX_INLINE PxFlags<enumtype,storagetype> &operator&=(const PxFlags<enumtype,storagetype> &f);
		PX_INLINE PxFlags<enumtype,storagetype>  operator& (enumtype e) const;
		PX_INLINE PxFlags<enumtype,storagetype>  operator& (const PxFlags<enumtype,storagetype> &f) const;
		
		PX_INLINE PxFlags<enumtype,storagetype> &operator^=(enumtype e);
		PX_INLINE PxFlags<enumtype,storagetype> &operator^=(const PxFlags<enumtype,storagetype> &f);
		PX_INLINE PxFlags<enumtype,storagetype>  operator^ (enumtype e) const;
		PX_INLINE PxFlags<enumtype,storagetype>  operator^ (const PxFlags<enumtype,storagetype> &f) const;
		
		PX_INLINE PxFlags<enumtype,storagetype>  operator~ (void) const;
		
		PX_INLINE                                operator bool(void) const;
		PX_INLINE								 operator PxU8(void) const;
		PX_INLINE                                operator PxU16(void) const;
		PX_INLINE                                operator PxU32(void) const;

		PX_INLINE void                           clear(enumtype e);
	
	public:
		friend PX_INLINE PxFlags<enumtype,storagetype> operator&(enumtype a, PxFlags<enumtype,storagetype> &b)
		{
			PxFlags<enumtype,storagetype> out;
			out.mBits = a & b.mBits;
			return out;
		}

	private:
		storagetype  mBits;
	};

	#define PX_FLAGS_OPERATORS(enumtype, storagetype)                                                                                         \
		PX_INLINE PxFlags<enumtype, storagetype> operator|(enumtype a, enumtype b) { PxFlags<enumtype, storagetype> r(a); r |= b; return r; } \
		PX_INLINE PxFlags<enumtype, storagetype> operator&(enumtype a, enumtype b) { PxFlags<enumtype, storagetype> r(a); r &= b; return r; } \
		PX_INLINE PxFlags<enumtype, storagetype> operator~(enumtype a)             { return ~PxFlags<enumtype, storagetype>(a);             }

	#define PX_FLAGS_TYPEDEF(x, y)	typedef PxFlags<x::Enum, y> x##s;	\
	PX_FLAGS_OPERATORS(x::Enum, y)

	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>::PxFlags(void)
	{
		mBits = 0;
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>::PxFlags(enumtype e)
	{
		mBits = static_cast<storagetype>(e);
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>::PxFlags(const PxFlags<enumtype,storagetype> &f)
	{
		mBits = f.mBits;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>& PxFlags<enumtype,storagetype>::operator =(const PxFlags<enumtype,storagetype> &f)
	{
		mBits = f.mBits;
		return *this;
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>::PxFlags(storagetype b)
	{
		mBits = b;
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE bool PxFlags<enumtype,storagetype>::isSet(enumtype e) const
	{
		return (mBits & static_cast<storagetype>(e)) == static_cast<storagetype>(e);
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> &PxFlags<enumtype,storagetype>::set(enumtype e)
	{
		mBits = static_cast<storagetype>(e);
		return *this;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE bool PxFlags<enumtype,storagetype>::operator==(enumtype e) const
	{
		return mBits == static_cast<storagetype>(e);
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE bool PxFlags<enumtype,storagetype>::operator==(const PxFlags<enumtype,storagetype>& f) const
	{
		return mBits == f.mBits;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE bool PxFlags<enumtype,storagetype>::operator==(bool b) const
	{
		return ((bool)*this) == b;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE bool PxFlags<enumtype,storagetype>::operator!=(enumtype e) const
	{
		return mBits != static_cast<storagetype>(e);
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE bool PxFlags<enumtype,storagetype>::operator!=(const PxFlags<enumtype,storagetype> &f) const
	{
		return mBits != f.mBits;
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> &PxFlags<enumtype,storagetype>::operator =(enumtype e)
	{
		mBits = static_cast<storagetype>(e);
		return *this;
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> &PxFlags<enumtype,storagetype>::operator|=(enumtype e)
	{
		mBits |= static_cast<storagetype>(e);
		return *this;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> &PxFlags<enumtype,storagetype>::operator|=(const PxFlags<enumtype,storagetype> &f)
	{
		mBits |= f.mBits;
		return *this;
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> PxFlags<enumtype,storagetype>::operator| (enumtype e) const
	{
		PxFlags<enumtype,storagetype> out(*this);
		out |= e;
		return out;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> PxFlags<enumtype,storagetype>::operator| (const PxFlags<enumtype,storagetype> &f) const
	{
		PxFlags<enumtype,storagetype> out(*this);
		out |= f;
		return out;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> &PxFlags<enumtype,storagetype>::operator&=(enumtype e)
	{
		mBits &= static_cast<storagetype>(e);
		return *this;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> &PxFlags<enumtype,storagetype>::operator&=(const PxFlags<enumtype,storagetype> &f)
	{
		mBits &= f.mBits;
		return *this;
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> PxFlags<enumtype,storagetype>::operator&(enumtype e) const
	{
		PxFlags<enumtype,storagetype> out = *this;
		out.mBits &= static_cast<storagetype>(e);
		return out;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>  PxFlags<enumtype,storagetype>::operator& (const PxFlags<enumtype,storagetype> &f) const
	{
		PxFlags<enumtype,storagetype> out = *this;
		out.mBits &= f.mBits;
		return out;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> &PxFlags<enumtype,storagetype>::operator^=(enumtype e)
	{
		mBits ^= static_cast<storagetype>(e);
		return *this;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> &PxFlags<enumtype,storagetype>::operator^=(const PxFlags<enumtype,storagetype> &f)
	{
		mBits ^= f.mBits;
		return *this;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> PxFlags<enumtype,storagetype>::operator^ (enumtype e) const
	{
		PxFlags<enumtype,storagetype> out = *this;
		out.mBits ^= static_cast<storagetype>(e);
		return out;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> PxFlags<enumtype,storagetype>::operator^ (const PxFlags<enumtype,storagetype> &f) const
	{
		PxFlags<enumtype,storagetype> out = *this;
		out.mBits ^= f.mBits;
		return out;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype> PxFlags<enumtype,storagetype>::operator~ (void) const
	{
		PxFlags<enumtype,storagetype> out;
		out.mBits = (storagetype)~mBits;
		return out;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>::operator bool(void) const
	{
		return mBits ? true : false;
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>::operator PxU8(void) const
	{
		return static_cast<PxU8>(mBits);
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>::operator PxU16(void) const
	{
		return static_cast<PxU16>(mBits);
	}
	
	template<typename enumtype, typename storagetype>
	PX_INLINE PxFlags<enumtype,storagetype>::operator PxU32(void) const
	{
		return static_cast<PxU32>(mBits);
	}

	template<typename enumtype, typename storagetype>
	PX_INLINE void PxFlags<enumtype,storagetype>::clear(enumtype e)
	{
		mBits &= ~static_cast<storagetype>(e);
	}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif // #ifndef PX_FOUNDATION_PX_FLAGS_H
