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


#ifndef PX_FOUNDATION_PX_VEC4_H
#define PX_FOUNDATION_PX_VEC4_H
/** \addtogroup foundation
@{
*/
#include "foundation/PxMath.h"
#include "foundation/PxVec3.h"
#include "foundation/PxAssert.h"


/**
\brief 4 Element vector class.

This is a 4-dimensional vector class with public data members.
*/
#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxVec4
{
public:

	/**
	\brief default constructor leaves data uninitialized.
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4() {}

	/**
	\brief zero constructor.
	*/
	PX_CUDA_CALLABLE PX_FORCE_INLINE PxVec4(PxZERO r): x(0.0f), y(0.0f), z(0.0f), w(0.0f) 
	{
		PX_UNUSED(r);
	}

	/**
	\brief Assigns scalar parameter to all elements.

	Useful to initialize to zero or one.

	\param[in] a Value to assign to elements.
	*/
	explicit PX_CUDA_CALLABLE PX_INLINE PxVec4(PxReal a): x(a), y(a), z(a), w(a) {}

	/**
	\brief Initializes from 3 scalar parameters.

	\param[in] nx Value to initialize X component.
	\param[in] ny Value to initialize Y component.
	\param[in] nz Value to initialize Z component.
	\param[in] nw Value to initialize W component.
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4(PxReal nx, PxReal ny, PxReal nz, PxReal nw): x(nx), y(ny), z(nz), w(nw) {}


	/**
	\brief Initializes from 3 scalar parameters.

	\param[in] v Value to initialize the X, Y, and Z components.
	\param[in] nw Value to initialize W component.
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4(const PxVec3& v, PxReal nw): x(v.x), y(v.y), z(v.z), w(nw) {}


	/**
	\brief Initializes from an array of scalar parameters.

	\param[in] v Value to initialize with.
	*/
	explicit PX_CUDA_CALLABLE PX_INLINE PxVec4(const PxReal v[]): x(v[0]), y(v[1]), z(v[2]), w(v[3]) {}

	/**
	\brief Copy ctor.
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4(const PxVec4& v): x(v.x), y(v.y), z(v.z), w(v.w) {}

	//Operators

	/**
	\brief Assignment operator
	*/
	PX_CUDA_CALLABLE PX_INLINE	PxVec4&	operator=(const PxVec4& p)			{ x = p.x; y = p.y; z = p.z; w = p.w;	return *this;		}

	/**
	\brief element access
	*/
	PX_DEPRECATED PX_CUDA_CALLABLE PX_INLINE PxReal& operator[](int index)
	{
		PX_ASSERT(index>=0 && index<=3);

		return reinterpret_cast<PxReal*>(this)[index];
	}
	
	/**
	\brief element access
	*/
	PX_CUDA_CALLABLE PX_INLINE PxReal& operator[](unsigned int index)
	{
		PX_ASSERT(index<=3);

		return reinterpret_cast<PxReal*>(this)[index];
	}

	/**
	\brief element access
	*/
	PX_DEPRECATED PX_CUDA_CALLABLE PX_INLINE const PxReal& operator[](int index) const
	{
		PX_ASSERT(index>=0 && index<=3);

		return reinterpret_cast<const PxReal*>(this)[index];
	}

	/**
	\brief element access
	*/
	PX_CUDA_CALLABLE PX_INLINE const PxReal& operator[](unsigned int index) const
	{
		PX_ASSERT(index<=3);

		return reinterpret_cast<const PxReal*>(this)[index];
	}

	/**
	\brief returns true if the two vectors are exactly equal.
	*/
	PX_CUDA_CALLABLE PX_INLINE bool operator==(const PxVec4&v) const	{ return x == v.x && y == v.y && z == v.z && w == v.w; }

	/**
	\brief returns true if the two vectors are not exactly equal.
	*/
	PX_CUDA_CALLABLE PX_INLINE bool operator!=(const PxVec4&v) const	{ return x != v.x || y != v.y || z != v.z || w!= v.w; }

	/**
	\brief tests for exact zero vector
	*/
	PX_CUDA_CALLABLE PX_INLINE bool isZero()	const					{ return x==0 && y==0 && z == 0 && w == 0;			}

	/**
	\brief returns true if all 3 elems of the vector are finite (not NAN or INF, etc.)
	*/
	PX_CUDA_CALLABLE PX_INLINE bool isFinite() const
	{
		return PxIsFinite(x) && PxIsFinite(y) && PxIsFinite(z) && PxIsFinite(w);
	}

	/**
	\brief is normalized - used by API parameter validation
	*/
	PX_CUDA_CALLABLE PX_INLINE bool isNormalized() const
	{
		const float unitTolerance = 1e-4f;
		return isFinite() && PxAbs(magnitude()-1)<unitTolerance;
	}


	/**
	\brief returns the squared magnitude

	Avoids calling PxSqrt()!
	*/
	PX_CUDA_CALLABLE PX_INLINE PxReal magnitudeSquared() const		{	return x * x + y * y + z * z + w * w;		}

	/**
	\brief returns the magnitude
	*/
	PX_CUDA_CALLABLE PX_INLINE PxReal magnitude() const				{	return PxSqrt(magnitudeSquared());		}

	/**
	\brief negation
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4 operator -() const
	{
		return PxVec4(-x, -y, -z, -w);
	}

	/**
	\brief vector addition
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4 operator +(const PxVec4& v) const		{	return PxVec4(x + v.x, y + v.y, z + v.z, w + v.w);	}

	/**
	\brief vector difference
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4 operator -(const PxVec4& v) const		{	return PxVec4(x - v.x, y - v.y, z - v.z, w - v.w);	}

	/**
	\brief scalar post-multiplication
	*/

	PX_CUDA_CALLABLE PX_INLINE PxVec4 operator *(PxReal f) const				{	return PxVec4(x * f, y * f, z * f, w * f);		}

	/**
	\brief scalar division
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4 operator /(PxReal f) const
	{
		f = 1.0f / f; 
		return PxVec4(x * f, y * f, z * f, w * f);
	}

	/**
	\brief vector addition
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4& operator +=(const PxVec4& v)
	{
		x += v.x;
		y += v.y;
		z += v.z;
		w += v.w;
		return *this;
	}
	
	/**
	\brief vector difference
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4& operator -=(const PxVec4& v)
	{
		x -= v.x;
		y -= v.y;
		z -= v.z;
		w -= v.w;
		return *this;
	}

	/**
	\brief scalar multiplication
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4& operator *=(PxReal f)
	{
		x *= f;
		y *= f;
		z *= f;
		w *= f;
		return *this;
	}
	/**
	\brief scalar division
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4& operator /=(PxReal f)
	{
		f = 1.0f/f;
		x *= f;
		y *= f;
		z *= f;
		w *= f;
		return *this;
	}

	/**
	\brief returns the scalar product of this and other.
	*/
	PX_CUDA_CALLABLE PX_INLINE PxReal dot(const PxVec4& v) const		
	{	
		return x * v.x + y * v.y + z * v.z + w * v.w;				
	}

	/** return a unit vector */

	PX_CUDA_CALLABLE PX_INLINE PxVec4 getNormalized() const
	{
		PxReal m = magnitudeSquared();
		return m>0.0f ? *this * PxRecipSqrt(m) : PxVec4(0,0,0,0);
	}


	/**
	\brief normalizes the vector in place
	*/
	PX_CUDA_CALLABLE PX_INLINE PxReal normalize()
	{
		PxReal m = magnitude();
		if (m>0.0f) 
			*this /= m;
		return m;
	}

	/**
	\brief a[i] * b[i], for all i.
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4 multiply(const PxVec4& a) const
	{
		return PxVec4(x*a.x, y*a.y, z*a.z, w*a.w);
	}

	/**
	\brief element-wise minimum
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4 minimum(const PxVec4& v) const
	{ 
		return PxVec4(PxMin(x, v.x), PxMin(y,v.y), PxMin(z,v.z), PxMin(w,v.w));	
	}

	/**
	\brief element-wise maximum
	*/
	PX_CUDA_CALLABLE PX_INLINE PxVec4 maximum(const PxVec4& v) const
	{ 
		return PxVec4(PxMax(x, v.x), PxMax(y,v.y), PxMax(z,v.z), PxMax(w,v.w));	
	} 

	PX_CUDA_CALLABLE PX_INLINE PxVec3 getXYZ() const
	{
		return PxVec3(x,y,z);
	}

	/**
	\brief set vector elements to zero
	*/
	PX_CUDA_CALLABLE PX_INLINE void setZero() {	x = y = z = w = 0.0f; }

	PxReal x,y,z,w;
};


PX_CUDA_CALLABLE static PX_INLINE PxVec4 operator *(PxReal f, const PxVec4& v)
{
	return PxVec4(f * v.x, f * v.y, f * v.z, f * v.w);
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif // PX_FOUNDATION_PX_VEC4_H
