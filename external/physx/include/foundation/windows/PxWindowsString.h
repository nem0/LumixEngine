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


#ifndef PX_FOUNDATION_PX_WINDOWS_STRING_H
#define PX_FOUNDATION_PX_WINDOWS_STRING_H

#include "foundation/Px.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#pragma warning(push)
#pragma warning(disable: 4996)

#ifndef PX_DOXYGEN
namespace physx
{
#endif

	PX_INLINE void PxStrcpy(char* dest, size_t size, const char* src) {::strcpy_s(dest, size, src);}
	PX_INLINE void PxStrcat(char* dest, size_t size, const char* src) {::strcat_s(dest, size, src);}
	PX_INLINE PxI32 PxVsprintf(char* dest, size_t size, const char* src, va_list arg)
	{
		PxI32 r = ::vsprintf_s(dest, size, src, arg);

		return r;
	}
	PX_INLINE PxI32 PxStricmp(const char *str, const char *str1) {return(::_stricmp(str, str1));}

#ifndef PX_DOXYGEN
} // namespace physx
#endif

#pragma warning(pop)

#endif
