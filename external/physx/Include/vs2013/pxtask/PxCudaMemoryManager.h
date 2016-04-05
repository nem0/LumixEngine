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

#ifndef PX_CUDA_MEMORY_MANAGER_H
#define PX_CUDA_MEMORY_MANAGER_H

#include "foundation/PxSimpleTypes.h"

// some macros to keep the source code more readable
#define NV_ALLOC_INFO(name, ID) __FILE__, __LINE__, name, physx::PxAllocId::ID
#define NV_ALLOC_INFO_PARAMS_DECL(p0, p1, p2, p3)  const char* file = p0, int line = p1, const char* allocName = p2, physx::PxAllocId::Enum allocId = physx::PxAllocId::p3
#define NV_ALLOC_INFO_PARAMS_DEF()  const char* file, int line, const char* allocName, physx::PxAllocId::Enum allocId
#define NV_ALLOC_INFO_PARAMS_INPUT()  file, line, allocName, allocId
#define NV_ALLOC_INFO_PARAMS_INPUT_INFO(info) info.getFileName(), info.getLine(), info.getAllocName(), info.getAllocId()

#ifndef NULL // don't want to include <string.h>
#define NULL 0
#endif

#ifndef PX_DOXYGEN
namespace physx
{
#endif

PX_PUSH_PACK_DEFAULT

/** \brief ID of the Feature which owns/allocated memory from the heap
 *
 * Maximum of 64k IDs allowed.
 */
struct PxAllocId
{
    /**
     * \brief ID of the Feature which owns/allocated memory from the heap
     */
	enum Enum
	{
		UNASSIGNED,		//!< default
		APEX,			//!< APEX stuff not further classified
		PARTICLES,		//!< all particle related
		GPU_UTIL,		//!< e.g. RadixSort (used in SPH and deformable self collision)
		CLOTH,			//!< all cloth related
		NUM_IDS			//!< number of IDs, be aware that ApexHeapStats contains PxAllocIdStats[NUM_IDS]
	};
};

/// \brief memory type managed by a heap
struct PxCudaBufferMemorySpace
{
    /**
     * \brief memory type managed by a heap
     */
	enum Enum
	{
		T_GPU,
		T_PINNED_HOST,
		T_WRITE_COMBINED,
		T_HOST,
		COUNT
	};
};

/// \brief class to track allocation statistics, see PxgMirrored
class PxAllocInfo
{
public:
    /**
     * \brief AllocInfo default constructor
     */
	PxAllocInfo() {}

    /**
     * \brief AllocInfo constructor that initializes all of the members
     */
	PxAllocInfo(const char* file, int line, const char* allocName, PxAllocId::Enum allocId)
		: mFileName(file)
		, mLine(line)
		, mAllocName(allocName)
		, mAllocId(allocId)
	{}

	/// \brief get the allocation file name
	inline	const char*			getFileName() const
	{
		return mFileName;
	}

	/// \brief get the allocation line
	inline	int					getLine() const
	{
		return mLine;
	}

	/// \brief get the allocation name
	inline	const char*			getAllocName() const
	{
		return mAllocName;
	}

	/// \brief get the allocation ID
	inline	PxAllocId::Enum		getAllocId() const
	{
		return mAllocId;
	}

private:
	const char*			mFileName;
	int					mLine;
	const char*			mAllocName;
	PxAllocId::Enum		mAllocId;
};

/// \brief statistics collected per AllocationId by HeapManager.
struct PxAllocIdStats
{
	size_t size;		//!< currently allocated memory by this ID
	size_t maxSize;		//!< max allocated memory by this ID
	size_t elements;	//!< number of current allocations by this ID
	size_t maxElements;	//!< max number of allocations by this ID
};

class PxCudaMemoryManager;
typedef size_t PxCudaBufferPtr;

/// \brief Hint flag to tell how the buffer will be used
struct PxCudaBufferFlags
{
/// \brief Enumerations for the hint flag to tell how the buffer will be used
	enum Enum
	{
		F_READ			= (1 << 0),
		F_WRITE			= (1 << 1),
		F_READ_WRITE	= F_READ | F_WRITE
	};
};


/// \brief Memory statistics struct returned by CudaMemMgr::getStats()
struct PxCudaMemoryManagerStats
{

	size_t			heapSize;		//!< Size of all pages allocated for this memory type (allocated + free).
	size_t			totalAllocated; //!< Size occupied by the current allocations.
	size_t			maxAllocated;	//!< High water mark of allocations since the SDK was created.
	PxAllocIdStats	allocIdStats[PxAllocId::NUM_IDS]; //!< Stats for each allocation ID, see PxAllocIdStats
};


/// \brief Buffer type: made of hint flags and the memory space (Device Memory, Pinned Host Memory, ...)
struct PxCudaBufferType
{
	/// \brief PxCudaBufferType copy constructor
	PX_INLINE PxCudaBufferType(const PxCudaBufferType& t)
		: memorySpace(t.memorySpace)
		, flags(t.flags)
	{}
	
	/// \brief PxCudaBufferType constructor to explicitely assign members
	PX_INLINE PxCudaBufferType(PxCudaBufferMemorySpace::Enum _memSpace, PxCudaBufferFlags::Enum _flags)
		: memorySpace(_memSpace)
		, flags(_flags)
	{}

	PxCudaBufferMemorySpace::Enum	memorySpace; 	//!< specifies which memory space for the buffer 
	PxCudaBufferFlags::Enum			flags;			//!< specifies the usage flags for the buffer
};


/// \brief Buffer which keeps informations about allocated piece of memory.
class PxCudaBuffer
{
public:
	/// Retrieves the manager over which the buffer was allocated.
	virtual	PxCudaMemoryManager*			getCudaMemoryManager() const = 0;

	/// Releases the buffer and the memory it used, returns true if successful.
	virtual	bool						free() = 0;

	/// Realloc memory. Use to shrink or resize the allocated chunk of memory of this buffer.
	/// Returns true if successful. Fails if the operation would change the address and need a memcopy.
	/// In that case the user has to allocate, copy and free the memory with separate steps.
	/// Realloc to size 0 always returns false and doesn't change the state.
	virtual	bool						realloc(size_t size, NV_ALLOC_INFO_PARAMS_DECL(NULL, 0, NULL, UNASSIGNED)) = 0;

	/// Returns the type of the allocated memory.
	virtual	const PxCudaBufferType&		getType() const = 0;

	/// Returns the pointer to the allocated memory.
	virtual	PxCudaBufferPtr				getPtr() const = 0;

	/// Returns the size of the allocated memory.
	virtual	size_t						getSize() const = 0;

protected:
    /// \brief protected destructor
    virtual ~PxCudaBuffer() {}
};


/// \brief Allocator class for different kinds of CUDA related memory.
class PxCudaMemoryManager
{
public:
	/// Allocate memory of given type and size. Returns a CudaBuffer if successful. Returns NULL if failed.
	virtual PxCudaBuffer*				alloc(const PxCudaBufferType& type, size_t size, NV_ALLOC_INFO_PARAMS_DECL(NULL, 0, NULL, UNASSIGNED)) = 0;

	/// Basic heap allocator without PxCudaBuffer
	virtual PxCudaBufferPtr				alloc(PxCudaBufferMemorySpace::Enum memorySpace, size_t size, NV_ALLOC_INFO_PARAMS_DECL(NULL, 0, NULL, UNASSIGNED)) = 0;

	/// Basic heap deallocator without PxCudaBuffer
	virtual bool						free(PxCudaBufferMemorySpace::Enum memorySpace, PxCudaBufferPtr addr) = 0;

	/// Basic heap realloc without PxCudaBuffer
	virtual bool						realloc(PxCudaBufferMemorySpace::Enum memorySpace, PxCudaBufferPtr addr, size_t size, NV_ALLOC_INFO_PARAMS_DECL(NULL, 0, NULL, UNASSIGNED)) = 0;

	/// Retrieve stats for the memory of given type. See PxCudaMemoryManagerStats.
	virtual void						getStats(const PxCudaBufferType& type, PxCudaMemoryManagerStats& outStats) = 0;

	/// Ensure that a given amount of free memory is available. Triggers CUDA allocations in size of (2^n * pageSize) if necessary.
	/// Returns false if page allocations failed.
	virtual bool						reserve(const PxCudaBufferType& type, size_t size) = 0;

	/// Set the page size. The managed memory grows by blocks 2^n * pageSize. Page allocations trigger CUDA driver allocations,
	/// so the page size should be reasonably big. Returns false if input size was invalid, i.e. not power of two.
	/// Default is 2 MB.
	virtual bool						setPageSize(const PxCudaBufferType& type, size_t size) = 0;

	/// Set the upper limit until which pages of a given memory type can be allocated.
	/// Reducing the max when it is already hit does not shrink the memory until it is deallocated by releasing the buffers which own the memory.
	virtual bool						setMaxMemorySize(const PxCudaBufferType& type, size_t size) = 0;

	/// Returns the base size. The base memory block stays persistently allocated over the SDKs life time.
	virtual size_t						getBaseSize(const PxCudaBufferType& type) = 0;

	/// Returns the currently set page size. The memory grows and shrinks in blocks of size (2^n pageSize)
	virtual size_t						getPageSize(const PxCudaBufferType& type) = 0;

	/// Returns the upper limit until which the manager is allowed to allocate additional pages from the CUDA driver.
	virtual size_t						getMaxMemorySize(const PxCudaBufferType& type) = 0;

	/// Get device mapped pinned host mem ptr. Operation only valid for memory space PxCudaBufferMemorySpace::T_PINNED_HOST.
	virtual PxCudaBufferPtr				getMappedPinnedPtr(PxCudaBufferPtr hostPtr) = 0;

protected:
    /// \brief protected destructor
    virtual ~PxCudaMemoryManager() {}
};

PX_POP_PACK

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif
