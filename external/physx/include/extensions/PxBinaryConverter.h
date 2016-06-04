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


#ifndef PX_BINARY_CONVERTER_H
#define PX_BINARY_CONVERTER_H
/** \addtogroup extensions
@{
*/

#include "common/PxPhysXCommonConfig.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxInputStream;
class PxOutputStream;

struct PxConverterReportMode
{
	enum Enum
	{
		eNONE,		//!< Silent mode. If enabled, no information is sent to the error stream.
		eNORMAL,	//!< Normal mode. If enabled, only important information is sent to the error stream.
		eVERBOSE	//!< Verbose mode. If enabled, detailed information is sent to the error stream.
	};
};


/**
\brief Binary converter for serialized streams.

The binary converter class is targeted at converting binary streams from authoring platforms, 
such as windows, osx or linux to any game runtime platform supported by PhysX. Particularly 
it is currently not supported to run the converter on a platforms that has an endian mismatch 
with the platform corresponding to the source binary file and source meta data. 

If you want to use multiple threads for batch conversions, please create one instance
of this class for each thread.

@see PxSerialization.createBinaryConverter
*/
class PxBinaryConverter
{
public:

	/**
	\brief Releases binary converter
	*/
	virtual		void	release()																			= 0;

	/**
	\brief Sets desired report mode.

	\param[in] mode	Report mode
	*/
	virtual		void	setReportMode(PxConverterReportMode::Enum mode)										= 0;

	/**
	\brief Setups source and target meta-data streams

	The source meta data provided needs to have the same endianness as the platform the converter is run on.
	The meta data needs to be set before calling the conversion method.

	\param[in] srcMetaData	Source platform's meta-data stream
	\param[in] dstMetaData	Target platform's meta-data stream

	\return True if success
	*/
	virtual		bool	setMetaData(PxInputStream& srcMetaData, PxInputStream& dstMetaData)					= 0;

	/**
	\brief Converts binary stream from source platform to target platform

	The converter needs to be configured with source and destination meta data before calling the conversion method. 
	The source meta data needs to correspond to the same platform as the source binary data.

	\param[in] srcStream	Source stream
	\param[in] srcSize		Number of bytes to convert
	\param[in] targetStream	Target stream

	\return True if success
	*/
	virtual		bool	convert(PxInputStream& srcStream, PxU32 srcSize, PxOutputStream& targetStream)		= 0;


protected:
						PxBinaryConverter()		{}
	virtual				~PxBinaryConverter()	{}
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
