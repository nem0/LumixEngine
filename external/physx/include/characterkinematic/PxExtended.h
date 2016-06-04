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


#ifndef PX_PHYSICS_CCT_EXTENDED
#define PX_PHYSICS_CCT_EXTENDED
/** \addtogroup character
  @{
*/

// This needs to be included in Foundation just for the debug renderer

#include "PxPhysXConfig.h"
#include "foundation/PxTransform.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

// This has to be done here since it also changes the top-level "Nx" API (as well as "Nv" and "Np" ones)
#define PX_BIG_WORLDS

#ifdef PX_BIG_WORLDS
typedef	double	PxExtended;
#define	PX_MAX_EXTENDED	PX_MAX_F64
#define PxExtendedAbs(x)	fabs(x)

struct PxExtendedVec3
{
	PX_INLINE	PxExtendedVec3()																	{}
	PX_INLINE	PxExtendedVec3(PxExtended _x, PxExtended _y, PxExtended _z) : x(_x), y(_y), z(_z)	{}

	PX_INLINE	bool isZero()	const
	{
		if(x!=0.0 || y!=0.0 || z!=0.0)	return false;
		return true;
	}

	PX_INLINE PxExtended	dot(const PxVec3& v) const
	{
		return x * v.x + y * v.y + z * v.z;
	}

	PX_INLINE	PxExtended distanceSquared(const PxExtendedVec3& v) const
	{
		PxExtended dx = x - v.x;
		PxExtended dy = y - v.y;
		PxExtended dz = z - v.z;
		return dx * dx + dy * dy + dz * dz;
	}

	PX_INLINE PxExtended magnitudeSquared() const
	{
		return x * x + y * y + z * z;
	}

	PX_INLINE PxExtended magnitude() const
	{
		return PxSqrt(x * x + y * y + z * z);
	}

	PX_INLINE	PxExtended	normalize()
	{
		PxExtended m = magnitude();
		if (m)
		{
			const PxExtended il =  PxExtended(1.0) / m;
			x *= il;
			y *= il;
			z *= il;
		}
		return m;
	}

	PX_INLINE	bool isFinite()	const
	{
		return PxIsFinite(x) && PxIsFinite(y) && PxIsFinite(z);
	}

	PX_INLINE	void maximum(const PxExtendedVec3& v)
	{
		if (x < v.x) x = v.x;
		if (y < v.y) y = v.y;
		if (z < v.z) z = v.z;
	}


	PX_INLINE	void minimum(const PxExtendedVec3& v)
	{
		if (x > v.x) x = v.x;
		if (y > v.y) y = v.y;
		if (z > v.z) z = v.z;
	}

	PX_INLINE	void	set(PxExtended x_, PxExtended y_, PxExtended z_)
	{
		this->x = x_;
		this->y = y_;
		this->z = z_;
	}

	PX_INLINE void	setPlusInfinity()
	{
		x = y = z = PX_MAX_EXTENDED;
	}

	PX_INLINE void	setMinusInfinity()
	{
		x = y = z = -PX_MAX_EXTENDED;
	}

	PX_INLINE void	cross(const PxExtendedVec3& left, const PxVec3& right)
	{
		// temps needed in case left or right is this.
		PxExtended a = (left.y * right.z) - (left.z * right.y);
		PxExtended b = (left.z * right.x) - (left.x * right.z);
		PxExtended c = (left.x * right.y) - (left.y * right.x);

		x = a;
		y = b;
		z = c;
	}

	PX_INLINE void	cross(const PxExtendedVec3& left, const PxExtendedVec3& right)
	{
		// temps needed in case left or right is this.
		PxExtended a = (left.y * right.z) - (left.z * right.y);
		PxExtended b = (left.z * right.x) - (left.x * right.z);
		PxExtended c = (left.x * right.y) - (left.y * right.x);

		x = a;
		y = b;
		z = c;
	}

	PX_INLINE PxExtendedVec3 cross(const PxExtendedVec3& v) const
	{
		PxExtendedVec3 temp;
		temp.cross(*this,v);
		return temp;
	}

	PX_INLINE void	cross(const PxVec3& left, const PxExtendedVec3& right)
	{
		// temps needed in case left or right is this.
		PxExtended a = (left.y * right.z) - (left.z * right.y);
		PxExtended b = (left.z * right.x) - (left.x * right.z);
		PxExtended c = (left.x * right.y) - (left.y * right.x);

		x = a;
		y = b;
		z = c;
	}

	PX_INLINE	PxExtendedVec3		operator-()		const
	{
		return PxExtendedVec3(-x, -y, -z);
	}

	PX_INLINE	PxExtendedVec3&		operator+=(const PxExtendedVec3& v)
	{
		x += v.x;
		y += v.y;
		z += v.z;
		return *this;
	}

	PX_INLINE	PxExtendedVec3&		operator-=(const PxExtendedVec3& v)
	{
		x -= v.x;
		y -= v.y;
		z -= v.z;
		return *this;
	}

	PX_INLINE	PxExtendedVec3&		operator+=(const PxVec3& v)
	{
		x += PxExtended(v.x);
		y += PxExtended(v.y);
		z += PxExtended(v.z);
		return *this;
	}

	PX_INLINE	PxExtendedVec3&		operator-=(const PxVec3& v)
	{
		x -= PxExtended(v.x);
		y -= PxExtended(v.y);
		z -= PxExtended(v.z);
		return *this;
	}

	PX_INLINE	PxExtendedVec3&		operator*=(const PxReal& s)
	{
		x *= PxExtended(s);
		y *= PxExtended(s);
		z *= PxExtended(s);
		return *this;
	}

	PX_INLINE	PxExtendedVec3		operator+(const PxExtendedVec3& v)	const
	{
		return PxExtendedVec3(x + v.x, y + v.y, z + v.z);
	}

	PX_INLINE	PxVec3			operator-(const PxExtendedVec3& v)	const
	{
		return PxVec3(PxReal(x - v.x), PxReal(y - v.y), PxReal(z - v.z));
	}

	PX_INLINE	PxExtended&			operator[](int index)
	{
		PX_ASSERT(index>=0 && index<=2);

		return reinterpret_cast<PxExtended*>(this)[index];
	}


	PX_INLINE	PxExtended			operator[](int index) const
	{
		PX_ASSERT(index>=0 && index<=2);

		return reinterpret_cast<const PxExtended*>(this)[index];
	}

	PxExtended x,y,z;
};

	PX_FORCE_INLINE PxVec3 toVec3(const PxExtendedVec3& v)
	{
		return PxVec3(float(v.x), float(v.y), float(v.z));
	}

#else
// Big worlds not defined

typedef	PxVec3		PxExtendedVec3;
typedef	PxReal		PxExtended;
#define	PX_MAX_EXTENDED	PX_MAX_F32
#define PxExtendedAbs(x)	fabsf(x)
#endif

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
