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


#ifndef PX_VISUALDEBUGGER_H
#define PX_VISUALDEBUGGER_H


#ifdef PX_SUPPORT_VISUAL_DEBUGGER
#define PX_SUPPORT_VISUAL_DEBUGGER 1
#include "foundation/PxSimpleTypes.h"
#include "foundation/PxVec3.h"
#else
#define PX_SUPPORT_VISUAL_DEBUGGER 0
#endif

/** \addtogroup pvd
@{
*/

#include "foundation/PxTransform.h"
#include "foundation/PxErrors.h"
#include "foundation/PxFlags.h"

#ifndef PX_DOXYGEN
namespace physx { namespace debugger { namespace comm {
#endif
	class PvdDataStream;
	class PvdConnection;
#ifndef PX_DOXYGEN
}}}
#endif

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxScene;

/**
\brief PVD Flags.
*/
struct PxVisualDebuggerFlag
{
	enum Enum
	{
		eTRANSMIT_CONTACTS		= (1 << 0),	//! Transmits contact stream to PVD. Disabled by default.
		eTRANSMIT_SCENEQUERIES	= (1 << 1),	//! Transmits scene query stream to PVD. Disabled by default.
		eTRANSMIT_CONSTRAINTS	= (1 << 2)  //! Transmits constraints visualize stream to PVD. Disabled by default.
	};
};

/**
\brief Bitfield that contains a set of raised flags defined in PxVisualDebuggerFlag.

@see PxVisualDebuggerFlag
*/
typedef PxFlags<PxVisualDebuggerFlag::Enum, PxU8> PxVisualDebuggerFlags;
PX_FLAGS_OPERATORS(PxVisualDebuggerFlag::Enum, PxU8)


/**
\brief Class to communicate with the PhysX Visual Debugger.
*/
class PxVisualDebugger
{
public:

	/**
	Disconnects the SDK from the PhysX Visual Debugger application.
	If we are still connected, this will kill the entire debugger connection.
	*/
	virtual void disconnect() = 0;

	/**
	 *	Checks if the connect state is paused. If it is, then this method will not
	 *	return until the connection state changes or pvd disconnects.
	 */
	virtual void checkConnection() = 0;

	/**
	returns the PVD connection that was passed to the SDK.
	returns NULL if no connection is present.
	*/
	virtual physx::debugger::comm::PvdConnection* getPvdConnection() = 0;

	/**
	\param scene The scene of which the PVD data stream should be returned.
	returns the PVD data stream of a given scene.
	returns NULL if no data stream is present.
	*/
	virtual physx::debugger::comm::PvdDataStream* getPvdDataStream(const PxScene& scene) = 0;

	/**
	\deprecated
	Controls if joint visualization info is sent to pvd.
	\param inViz true if visualizations info is sent to PVD.
	*/
	PX_DEPRECATED void PX_INLINE setVisualizeConstraints( bool inViz )	{ setVisualDebuggerFlag(PxVisualDebuggerFlag::eTRANSMIT_CONSTRAINTS, inViz); }
	/**
	\deprecated
	\return True when constraint viz info is sent to PVD.
	*/
	PX_DEPRECATED bool PX_INLINE isVisualizingConstraints()	{ return (getVisualDebuggerFlags() & PxVisualDebuggerFlag::eTRANSMIT_CONSTRAINTS) != 0;}

	/**
	Sets the PVD flag. See PxVisualDebuggerFlags.
	\param flag Flag to set.
	\param value value the flag gets set to.
	*/
	virtual void setVisualDebuggerFlag(PxVisualDebuggerFlag::Enum flag, bool value) = 0;

	/**
	Sets the PVD flags. See PxVisualDebuggerFlags.
	\param flags Flags to set.
	*/
	virtual void setVisualDebuggerFlags(PxVisualDebuggerFlags flags) = 0;

	/**
	Retrieves the PVD flags. See PxVisualDebuggerFlags.
	*/
	virtual PxU32 getVisualDebuggerFlags() = 0;

	/**
	Updates the pose of a PVD camera.
	\param name Name of camera to update.
	\param origin The origin of the camera.
	\param up The up vector of the camera. It should be the up vector of the game camera for PVD to update the view that matches the game. 
     The default up vector is the world up vector for a fixed PVD camera view.
	\param target The target vector of the camera.
	*/
	virtual void updateCamera(const char* name, const PxVec3& origin, const PxVec3& up, const PxVec3& target) = 0;

	/**
	\brief send an error message to pvd.
	\param code Error code, see #PxErrorCode
	\param message Message to display.
	\param file File error occured in.
	\param line Line number error occured on.
	*/
	virtual void sendErrorMessage(PxErrorCode::Enum code, const char* message, const char* file, PxU32 line) = 0;

protected:
	virtual ~PxVisualDebugger() {}
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif // PX_VISUALDEBUGGER_H
