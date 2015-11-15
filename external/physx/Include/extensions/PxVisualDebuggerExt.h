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


#ifndef PX_PHYSICS_EXTENSIONS_VISUAL_DEBUGGER_H
#define PX_PHYSICS_EXTENSIONS_VISUAL_DEBUGGER_H
/** \addtogroup extensions
  @{
*/
#include "pvd/PxVisualDebugger.h"
#include "foundation/Px.h"
#include "PxPhysXConfig.h"
#include "foundation/PxFlags.h"
#include "physxvisualdebuggersdk/PvdConnectionManager.h"
#include "physxvisualdebuggersdk/PvdConnection.h"


#ifndef PX_DOXYGEN
namespace physx { namespace debugger { namespace comm {
#endif
	class PvdConnectionManager;
	class PvdConnection;
#ifndef PX_DOXYGEN
}}}
#endif

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
	This class has a direct mapping to the PVD::TConnectionType datatype.  It is redefined here
	because not all classes including this header have the PVDSDK in their include path.

*/
struct PxVisualDebuggerConnectionFlag
{
	enum Enum
	{
		/**
			\brief Send debugging information to PVD.  
			
			This information is the actual object data
			of the rigid statics, shapes, articulations, etc.  Sending this information has
			a noticeable impact on performance and thus this flag should not be set
			if you want an accurate performance profile.
		*/
		eDEBUG = 1 << 0, 
		/**
			\brief Send profile information to PVD.

			This information populates PVD's profile view.  It has (at this time) negligible cost
			compared to Debug information and makes PVD *much* more useful so it is quite
			highly recommended.

			This flag works together with a PxCreatePhysics parameter,
			profileZoneManager.  Using both of them together allows SDK to send profile
			events to PVD.  
		*/
		ePROFILE = 1 << 1,
		/**
			\brief Send memory information to PVD.

			The PVD sdk side hooks into the Foundation memory controller and listens to 
			allocation/deallocation events.  This has a noticable hit on the first frame,
			however, this data is somewhat compressed and the PhysX SDK doesn't allocate much
			once it hits a steady state.  This information also has a fairly negligible
			impact and thus is also highly recommended.

			This flag works together with a PxCreatePhysics parameter,
			trackOutstandingAllocations.  Using both of them together allows users to have
			an accurate view of the overall memory usage of the simulation at the cost of
			a hashtable lookup per allocation/deallocation.  Again, PhysX makes a best effort
			attempt not to allocate or deallocate during simulation so this hashtable lookup
			tends to have no effect past the first frame.  
			
			Sending memory information without tracking outstanding allocations means that 
			PVD will accurate information about the state of the memory system before the 
			actual connection happened.
		*/
		eMEMORY = 1 << 2
	};
};

typedef physx::PxFlags<PxVisualDebuggerConnectionFlag::Enum, PxU32> PxVisualDebuggerConnectionFlags;
PX_FLAGS_OPERATORS( PxVisualDebuggerConnectionFlag::Enum, PxU32 )

/**
class that contains all the data relevant for updating and visualizing extensions like joints in PVD
*/
class PxVisualDebuggerExt
{
public:

	/**
		Connect to pvd using a network socket.  This blocks for at most inTimeoutInMilliseconds
		before returning a new connection (or nothing).  PVD needs to be started before this call
		is made.

		\note Since this call increment the reference count of PvdConnection,
		please call PvdConnection::release() after this funtion is called. 
		
		\param inMgr The manager to use to host the connection.
		\param inHost Host in x.x.x.x network notation
		\param inPort Port to connect to.  The default is 5425.
		\param inTimeoutInMilliseconds How long to block waiting for a new connection
		\param inConnectionType The type information you want sent over the connection.
	*/
	static PxVisualDebuggerConnection* createConnection( PxVisualDebuggerConnectionManager* inMgr
													, const char* inHost
													, int inPort //defaults to 5425
													, unsigned int inTimeoutInMilliseconds
													, PxVisualDebuggerConnectionFlags inConnectionType = getDefaultConnectionFlags() );

	
	/**
		Connect to pvd, writing out the connection data to a file stream.  You can then parse this data later
		with PVDUI.

		\note Since this call increment the reference count of PvdConnection,
		please call PvdConnection::release() after this funtion is called. 

		\param inMgr The manager to use to host the connection.
		\param filename The filename to write connection data.
		\param inConnectionType The type information you want sent over the connection.
	*/
	static PxVisualDebuggerConnection* createConnection( PxVisualDebuggerConnectionManager* inMgr
													, const char* filename
													, PxVisualDebuggerConnectionFlags inConnectionType = getDefaultConnectionFlags() );


	/**	get the default connection flags

	\return the default connection flags: debug data and profiling
	*/

	static PX_FORCE_INLINE PxVisualDebuggerConnectionFlags getDefaultConnectionFlags() { return PxVisualDebuggerConnectionFlags( PxVisualDebuggerConnectionFlag::eDEBUG | PxVisualDebuggerConnectionFlag::ePROFILE); }
	
	/**	get all connection flags

	\return all visual debugger connection flags: debug data, profiling and memory
	*/
	static PX_FORCE_INLINE PxVisualDebuggerConnectionFlags getAllConnectionFlags() { return PxVisualDebuggerConnectionFlags( PxVisualDebuggerConnectionFlag::eDEBUG | PxVisualDebuggerConnectionFlag::ePROFILE| PxVisualDebuggerConnectionFlag::eMEMORY ); }

};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif // PX_PHYSICS_EXTENSIONS_VISUAL_DEBUGGER_H
