/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#ifndef PVD_CONNECTION_FLAGS_H
#define PVD_CONNECTION_FLAGS_H

#include "foundation/PxFlags.h"

namespace physx { namespace debugger {
	
	struct PvdConnectionType
	{
		enum Enum
		{
			eDEBUG = 1 << 0,
			ePROFILE = 1 << 1,
			eMEMORY = 1 << 2
		};
	};

	typedef PxFlags<PvdConnectionType::Enum,PxU32> TConnectionFlagsType; 
	PX_FLAGS_OPERATORS(PvdConnectionType::Enum, PxU32 )
	
	static inline TConnectionFlagsType defaultConnectionFlags() { return TConnectionFlagsType( PvdConnectionType::eDEBUG | PvdConnectionType::ePROFILE | PvdConnectionType::eMEMORY ); }
	
	struct PvdConnectionState
	{
		enum Enum
		{
			eIDLE = 0,
			eRECORDINGORVIEWING,
			ePAUSED
		};
	};
}}

#endif
