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


#ifndef PX_FOUNDATION_PX_UNION_CAST_H
#define PX_FOUNDATION_PX_UNION_CAST_H

#include "foundation/PxPreprocessor.h"

/** \addtogroup foundation
@{
*/

#ifndef PX_DOXYGEN
namespace physx
{
#endif

template<class A, class B> PX_FORCE_INLINE A PxUnionCast(B b)
{
	union AB
	{
		AB(B bb) 
			: _b(bb)
		{
		}
		B _b;
		A _a;
	} u(b);
	return u._a;
}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */

#endif
