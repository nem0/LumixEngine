/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#ifndef PVD_ERROR_CODES_H
#define PVD_ERROR_CODES_H

namespace physx { namespace debugger {
	
	struct PvdErrorType
	{
		enum Enum
		{
			Success = 0,
			NetworkError,
			ArgumentError,
			InternalProblem
		};
	};

	typedef PvdErrorType::Enum PvdError;
}}

#endif
