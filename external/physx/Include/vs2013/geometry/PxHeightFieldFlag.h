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
// Copyright (c) 2008-2014 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_COLLISION_NXHEIGHTFIELDFLAG
#define PX_COLLISION_NXHEIGHTFIELDFLAG
/** \addtogroup geomutils
@{
*/

#include "foundation/PxFlags.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Describes the format of height field samples.
@see PxHeightFieldDesc.format PxHeightFieldDesc.samples
*/
struct PxHeightFieldFormat
{
	enum Enum
	{
		/**
		\brief Height field height data is 16 bit signed integers, followed by triangle materials. 
		
		Each sample is 32 bits wide arranged as follows:
		
		\image html heightFieldFormat_S16_TM.png

		1) First there is a 16 bit height value.
		2) Next, two one byte material indices, with the high bit of each byte reserved for special use.
		(so the material index is only 7 bits).
		The high bit of material0 is the tess-flag.
		The high bit of material1 is reserved for future use.
		
		There are zero or more unused bytes before the next sample depending on PxHeightFieldDesc.sampleStride, 
		where the application may eventually keep its own data.

		This is the only format supported at the moment.

		@see PxHeightFieldDesc.format PxHeightFieldDesc.samples
		*/
		eS16_TM = (1 << 0)
	};
};

/** 
\brief Determines the tessellation of height field cells.
@see PxHeightFieldDesc.format PxHeightFieldDesc.samples
*/
struct PxHeightFieldTessFlag
{
	enum Enum
	{
		/**
		\brief This flag determines which way each quad cell is subdivided.

		The flag lowered indicates subdivision like this: (the 0th vertex is referenced by only one triangle)
		
		\image html heightfieldTriMat2.PNG

		<pre>
		+--+--+--+---> column
		| /| /| /|
		|/ |/ |/ |
		+--+--+--+
		| /| /| /|
		|/ |/ |/ |
		+--+--+--+
		|
		|
		V row
		</pre>
		
		The flag raised indicates subdivision like this: (the 0th vertex is shared by two triangles)
		
		\image html heightfieldTriMat1.PNG

		<pre>
		+--+--+--+---> column
		|\ |\ |\ |
		| \| \| \|
		+--+--+--+
		|\ |\ |\ |
		| \| \| \|
		+--+--+--+
		|
		|
		V row
		</pre>
		
		@see PxHeightFieldDesc.format PxHeightFieldDesc.samples
		*/
		e0TH_VERTEX_SHARED = (1 << 0)
	};
};


/**
\brief Enum with flag values to be used in PxHeightFieldDesc.flags.
*/
struct PxHeightFieldFlag
{
	enum Enum
	{
		/**
		\brief Disable collisions with height field with boundary edges.
		
		Raise this flag if several terrain patches are going to be placed adjacent to each other, 
		to avoid a bump when sliding across.

		This flag is ignored in contact generation with sphere and capsule shapes.

		@see PxHeightFieldDesc.flags
		*/
		eNO_BOUNDARY_EDGES = (1 << 0)
	};
};

/**
\brief collection of set bits defined in PxHeightFieldFlag.

@see PxHeightFieldFlag
*/
typedef PxFlags<PxHeightFieldFlag::Enum,PxU16> PxHeightFieldFlags;
PX_FLAGS_OPERATORS(PxHeightFieldFlag::Enum,PxU16)

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
