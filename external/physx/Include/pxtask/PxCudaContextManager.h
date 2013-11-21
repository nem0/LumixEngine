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
// Copyright (c) 2008-2012 NVIDIA Corporation. All rights reserved.


#ifndef PX_CUDA_CONTEXT_MANAGER_H
#define PX_CUDA_CONTEXT_MANAGER_H

#include "pxtask/PxCudaMemoryManager.h"

/* Forward decl to avoid inclusion of cuda.h */
typedef struct CUctx_st *CUcontext;
typedef struct CUgraphicsResource_st *CUgraphicsResource;

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxProfileZoneManager;

namespace pxtask
{

/** \brief Possible graphic/CUDA interoperability modes for context */
struct CudaInteropMode
{
    /**
     * \brief Possible graphic/CUDA interoperability modes for context
     */
	enum Enum
	{
		NO_INTEROP = 0,
		D3D9_INTEROP,
		D3D10_INTEROP,
		D3D11_INTEROP,
		OGL_INTEROP,

		COUNT
	};
};

//! \brief Descriptor used to create a CudaContextManager
class CudaContextManagerDesc
{
public:
    /**
     * \brief The CUDA context to manage
     *
     * If left NULL, the CudaContextManager will create a new context.  If
     * graphicsDevice is also not NULL, this new CUDA context will be bound to
     * that graphics device, enabling the use of CUDA/Graphics interop features.
     *
     * If ctx is not NULL, the specified context must be applied to the thread
     * that is allocating the CudaContextManager at creation time (aka, it
     * cannot be popped).  The CudaContextManager will take ownership of the
     * context until the manager is released.  All access to the context must be
     * gated by lock acquisition.
     *
     * If the user provides a context for the CudaContextManager, the context
     * _must_ have either been created on the GPU ordinal returned by
     * getSuggestedCudaDeviceOrdinal() or on your graphics device.
     *
     * It is perfectly acceptable to allocate device or host pinned memory from
     * the context outside the scope of the CudaMemoryManager, so long as you
     * manage its eventual cleanup.
     */
	CUcontext            *ctx;

    /**
     * \brief D3D device pointer or OpenGl context handle
     *
     * Only applicable when ctx is NULL, thus forcing a new context to be
     * created.  In that case, the created context will be bound to this
     * graphics device.
     */
	void	             *graphicsDevice;

#if defined(PX_WINDOWS)
	/**
	  * \brief Application-specific GUID
	  *
	  * If your application employs PhysX modules that use CUDA you need to use a GUID 
	  * so that patches for new architectures can be released for your game.You can obtain a GUID for your 
	  * application from Nvidia.
	  */
	const char*			 appGUID;
#endif
    /**
     * \brief The CUDA/Graphics interop mode of this context
     *
     * If ctx is NULL, this value describes the nature of the graphicsDevice
     * pointer provided by the user.  Else it describes the nature of the
     * context provided by the user.
     */
	CudaInteropMode::Enum interopMode;


    /**
     * \brief Size of persistent memory
     *
     * This memory is allocated up front and stays allocated until the
     * CudaContextManager is released.  Size is in bytes, has to be power of two
     * and bigger than the page size.  Set to 0 to only use dynamic pages.
     *
     * Note: On Vista O/S and above, there is a per-memory allocation overhead
     * to every CUDA work submission, so we recommend that you carefully tune
     * this initial base memory size to closely approximate the ammount of
     * memory your application will consume.
     */
	PxU32		memoryBaseSize[CudaBufferMemorySpace::COUNT];

    /**
     * \brief Size of memory pages
     *
     * The memory manager will dynamically grow and shrink in blocks multiple of
     * this page size. Size has to be power of two and bigger than 0.
     */
	PxU32		memoryPageSize[CudaBufferMemorySpace::COUNT];

    /**
     * \brief Maximum size of memory that the memory manager will allocate
     */
	PxU32		maxMemorySize[CudaBufferMemorySpace::COUNT];

	PX_INLINE CudaContextManagerDesc()
	{
		ctx = NULL;
		interopMode = CudaInteropMode::NO_INTEROP;
		graphicsDevice = 0;
#if defined(PX_WINDOWS)
		appGUID  = NULL;
#endif
		for(PxU32 i = 0; i < CudaBufferMemorySpace::COUNT; i++)
		{
			memoryBaseSize[i] = 0;
			memoryPageSize[i] = 2 * 1024*1024;
			maxMemorySize[i] = PX_MAX_U32;
		}
	}
};


/**
 * \brief Manages memory, thread locks, and task scheduling for a CUDA context
 *
 * A CudaContextManager manages access to a single CUDA context, allowing it to
 * be shared between multiple scenes.   Memory allocations are dynamic: starting
 * with an initial heap size and growing on demand by a configurable page size.
 * The context must be acquired from the manager before using any CUDA APIs.
 *
 * The CudaContextManager is based on the CUDA driver API and explictly does not
 * support the the CUDA runtime API (aka, CUDART).
 *
 * To enable CUDA use by an APEX scene, a CudaContextManager must be created
 * (supplying your own CUDA context, or allowing a new context to be allocated
 * for you), the GpuDispatcher for that context is retrieved via the
 * getGpuDispatcher() method, and this is assigned to the TaskManager that is
 * given to the scene via its NxApexSceneDesc.
 */
class CudaContextManager
{
public:
    /**
     * \brief Acquire the CUDA context for the current thread
     *
     * Acquisitions are allowed to be recursive within a single thread.
     * You can acquire the context multiple times so long as you release
     * it the same count.
     *
     * The context must be acquired before using most CUDA functions.
     *
     * It is not necessary to acquire the CUDA context inside GpuTask
     * launch functions, because the GpuDispatcher will have already
     * acquired the context for its worker thread.  However it is not
     * harmfull to (re)acquire the context in code that is shared between
     * GpuTasks and non-task functions.
     */
    virtual void acquireContext() = 0;

    /**
     * \brief Release the CUDA context from the current thread
     *
     * The CUDA context should be released as soon as practically
     * possible, to allow other CPU threads (including the
     * GpuDispatcher) to work efficiently.
     */
    virtual void releaseContext() = 0;

    /**
     * \brief Return the CudaMemoryManager instance associated with this
     * CUDA context
     */
	virtual CudaMemoryManager *getMemoryManager() = 0;

    /**
     * \brief Return the GpuDispatcher instance associated with this
     * CUDA context
     */
	virtual class GpuDispatcher *getGpuDispatcher() = 0;

    /**
     * \brief Context manager has a valid CUDA context
     *
     * This method should be called after creating a CudaContextManager,
     * especially if the manager was responsible for allocating its own
     * CUDA context (desc.ctx == NULL).  If it returns false, there is
     * no point in assigning this manager's GpuDispatcher to a
     * TaskManager as it will be unable to execute GpuTasks.
     */
    virtual bool contextIsValid() const = 0;

	/* Query CUDA context and device properties, without acquiring context */

    virtual bool supportsArchSM10() const = 0;  //!< G80
    virtual bool supportsArchSM11() const = 0;  //!< G92
    virtual bool supportsArchSM12() const = 0;  //!< GT200
    virtual bool supportsArchSM13() const = 0;  //!< GT260
    virtual bool supportsArchSM20() const = 0;  //!< GF100
    virtual bool supportsArchSM30() const = 0;  //!< GK100
	virtual bool isIntegrated() const = 0;      //!< true if GPU is an integrated (MCP) part
	virtual bool hasDMAEngines() const = 0;     //!< true if GPU can overlap kernels and copies
	virtual bool canMapHostMemory() const = 0;  //!< true if GPU map host memory to GPU (0-copy)
	virtual int  getDriverVersion() const = 0;  //!< returns cached value of cuGetDriverVersion()
	virtual size_t getDeviceTotalMemBytes() const = 0; //!< returns cached value of device memory size
	virtual int	getMultiprocessorCount() const = 0; //!< returns cache value of SM unit count
    virtual unsigned int getClockRate() const = 0; //!< returns cached value of SM clock frequency
    virtual int  getSharedMemPerBlock() const = 0; //!< returns total amount of shared memory available per block in bytes
    virtual const char *getDeviceName() const = 0; //!< returns device name retrieved from driver
	virtual CudaInteropMode::Enum getInteropMode() const = 0; //!< interop mode the context was created with

    /* End query methods that don't require context to be acquired */

    /**
     * \brief Register a rendering resource with CUDA
     *
     * This function is called to register render resources (allocated
     * from OpenGL) with CUDA so that the memory may be shared
     * between the two systems.  This is only required for render
     * resources that are designed for interop use.  In APEX, each
     * render resource descriptor that could support interop has a
     * 'registerInCUDA' boolean variable.
     *
     * The function must be called again any time your graphics device
     * is reset, to re-register the resource.
     *
     * Returns true if the registration succeeded.  A registered
     * resource must be unregistered before it can be released.
     *
     * \param resource [OUT] the handle to the resource that can be used with CUDA
     * \param buffer [IN] GLuint buffer index to be mapped to cuda
     */
    virtual bool registerResourceInCudaGL(CUgraphicsResource &resource, PxU32 buffer) = 0;

     /**
     * \brief Register a rendering resource with CUDA
     *
     * This function is called to register render resources (allocated
     * from Direct3D) with CUDA so that the memory may be shared
     * between the two systems.  This is only required for render
     * resources that are designed for interop use.  In APEX, each
     * render resource descriptor that could support interop has a
     * 'registerInCUDA' boolean variable.
     *
     * The function must be called again any time your graphics device
     * is reset, to re-register the resource.
     *
     * Returns true if the registration succeeded.  A registered
     * resource must be unregistered before it can be released.
     *
     * \param resource [OUT] the handle to the resource that can be used with CUDA
     * \param resourcePointer [IN] A pointer to either IDirect3DResource9, or ID3D10Device, or ID3D11Resource to be registered.
     */
    virtual bool registerResourceInCudaD3D(CUgraphicsResource &resource, void *resourcePointer) = 0;

    /**
     * \brief Unregister a rendering resource with CUDA
     *
     * If a render resource was successfully registered with CUDA using
     * the registerResourceInCuda***() methods, this function must be called
     * to unregister the resource before the it can be released.
     */
    virtual bool unregisterResourceInCuda(CUgraphicsResource resource) = 0;

	/**
	 * \brief Determine if the user has configured a dedicated PhysX GPU in the NV Control Panel
	 * \note If using CUDA Interop, this will always return false
	 * \returns 1 if there is a dedicated PhysX GPU
	 * \returns 0 if there is NOT a dedicated PhysX GPU
	 * \returns -1 if the routine is not implemented
	*/
	virtual int	usingDedicatedPhysXGPU() const = 0;

    /**
     * \brief Release the CudaContextManager
     *
     * When the manager instance is released, it also releases its
     * GpuDispatcher instance and CudaMemoryManager.  Before the memory
     * manager is released, it frees all allocated memory pages.  If the
     * CudaContextManager created the CUDA context it was responsible
     * for, it also frees that context.
     *
     * Do not release the CudaContextManager if there are any scenes
     * using its GpuDispatcher.  Those scenes must be released first
     * since there is no safe way to remove a GpuDispatcher from a
     * TaskManager once the TaskManager has been given to a scene.
     *
     */
	virtual void release() = 0;

protected:

    /**
     * \brief protected destructor, use release() method
     */
    virtual ~CudaContextManager() {};
};

/**
 * \brief Convenience class for holding CUDA lock within a scope
 */
class ScopedCudaLock
{
public:
    /**
     * \brief ScopedCudaLock constructor
     */
	ScopedCudaLock(CudaContextManager& ctx) : mCtx(&ctx)
	{
		mCtx->acquireContext();
	}

    /**
     * \brief ScopedCudaLock destructor
     */
	~ScopedCudaLock()
	{
		mCtx->releaseContext();
	}

protected:

    /**
     * \brief CUDA context manager pointer (initialized in the the constructor)
     */
    CudaContextManager* mCtx;
};

/**
 * \brief Ask the NVIDIA control panel which GPU has been selected for use by
 * PhysX.  Returns -1 if no PhysX capable GPU is found or GPU PhysX has
 * been disabled.
 */
int                 getSuggestedCudaDeviceOrdinal(PxErrorCallback& errc);

/**
 * \brief Allocate a CUDA Context manager, complete with heaps and task dispatcher.
 * You only need one CUDA context manager per GPU device you intend to use for
 * CUDA tasks.  If mgr is NULL, no profiling of CUDA code will be possible.
 */
CudaContextManager* createCudaContextManager(PxFoundation& foundation, const CudaContextManagerDesc& desc, physx::PxProfileZoneManager* mgr);

/**
 * \brief get handle of physx GPU module
 */
#if defined(PX_WINDOWS)
void* loadPhysxGPUModule(const char* appGUID = NULL);
#else
void* loadPhysxGPUModule();
#endif
} // end pxtask namespace

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif
