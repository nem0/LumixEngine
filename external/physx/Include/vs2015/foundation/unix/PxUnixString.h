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


#ifndef PX_FOUNDATION_PX_UNIX_STRING_H
#define PX_FOUNDATION_PX_UNIX_STRING_H

#include "foundation/Px.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	PX_INLINE void PxStrcpy(char* dest, size_t size, const char* src)
	{
		::strncpy(dest, src, size);
	}

	PX_INLINE int PxStrcat(char* dest, size_t size, const char* src)
	{
		PX_UNUSED(size);
		::strcat(dest, src);
		return 0;
	}
	PX_INLINE int PxVsprintf(char* dest, size_t size, const char* src, va_list arg)
	{
		PX_UNUSED(size);
		int r = ::vsprintf( dest, src, arg );
		return r;
	}
	PX_INLINE int PxStricmp(const char *str, const char *str1) {return(::strcasecmp(str, str1));}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#endif
