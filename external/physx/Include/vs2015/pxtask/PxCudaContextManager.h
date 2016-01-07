/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


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

/** \brief Possible graphic/CUDA interoperability modes for context */
struct PxCudaInteropMode
{
    /**
     * \brief Possible graphic/CUDA interoperability modes for context
     */
	enum Enum
	{
		NO_INTEROP = 0,
		D3D10_INTEROP,
		D3D11_INTEROP,
		OGL_INTEROP,

		COUNT
	};
};


//! \brief Descriptor used to create a PxCudaContextManager
class PxCudaContextManagerDesc
{
public:
    /**
     * \brief The CUDA context to manage
     *
     * If left NULL, the PxCudaContextManager will create a new context.  If
     * graphicsDevice is also not NULL, this new CUDA context will be bound to
     * that graphics device, enabling the use of CUDA/Graphics interop features.
     *
     * If ctx is not NULL, the specified context must be applied to the thread
     * that is allocating the PxCudaContextManager at creation time (aka, it
     * cannot be popped).  The PxCudaContextManager will take ownership of the
     * context until the manager is released.  All access to the context must be
     * gated by lock acquisition.
     *
     * If the user provides a context for the PxCudaContextManager, the context
     * _must_ have either been created on the GPU ordinal returned by
     * PxGetSuggestedCudaDeviceOrdinal() or on your graphics device.
     *
     * It is perfectly acceptable to allocate device or host pinned memory from
     * the context outside the scope of the PxCudaMemoryManager, so long as you
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

#if PX_SUPPORT_GPU_PHYSX
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
	PxCudaInteropMode::Enum interopMode;


    /**
     * \brief Size of persistent memory
     *
     * This memory is allocated up front and stays allocated until the
     * PxCudaContextManager is released.  Size is in bytes, has to be power of two
     * and bigger than the page size.  Set to 0 to only use dynamic pages.
     *
     * Note: On Vista O/S and above, there is a per-memory allocation overhead
     * to every CUDA work submission, so we recommend that you carefully tune
     * this initial base memory size to closely approximate the amount of
     * memory your application will consume.
     */
	PxU32		memoryBaseSize[PxCudaBufferMemorySpace::COUNT];

    /**
     * \brief Size of memory pages
     *
     * The memory manager will dynamically grow and shrink in blocks multiple of
     * this page size. Size has to be power of two and bigger than 0.
     */
	PxU32		memoryPageSize[PxCudaBufferMemorySpace::COUNT];

    /**
     * \brief Maximum size of memory that the memory manager will allocate
     */
	PxU32		maxMemorySize[PxCudaBufferMemorySpace::COUNT];

	PX_INLINE PxCudaContextManagerDesc()
	{
		ctx = NULL;
		interopMode = PxCudaInteropMode::NO_INTEROP;
		graphicsDevice = 0;
#if PX_SUPPORT_GPU_PHYSX
		appGUID  = NULL;
#endif
		for(PxU32 i = 0; i < PxCudaBufferMemorySpace::COUNT; i++)
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
 * A PxCudaContextManager manages access to a single CUDA context, allowing it to
 * be shared between multiple scenes.   Memory allocations are dynamic: starting
 * with an initial heap size and growing on demand by a configurable page size.
 * The context must be acquired from the manager before using any CUDA APIs.
 *
 * The PxCudaContextManager is based on the CUDA driver API and explictly does not
 * support the the CUDA runtime API (aka, CUDART).
 *
 * To enable CUDA use by an APEX scene, a PxCudaContextManager must be created
 * (supplying your own CUDA context, or allowing a new context to be allocated
 * for you), the PxGpuDispatcher for that context is retrieved via the
 * getGpuDispatcher() method, and this is assigned to the TaskManager that is
 * given to the scene via its NxApexSceneDesc.
 */
class PxCudaContextManager
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
     * launch functions, because the PxGpuDispatcher will have already
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
     * PxGpuDispatcher) to work efficiently.
     */
    virtual void releaseContext() = 0;

    /**
     * \brief Return the PxCudaMemoryManager instance associated with this
     * CUDA context
     */
	virtual PxCudaMemoryManager *getMemoryManager() = 0;

    /**
     * \brief Return the PxGpuDispatcher instance associated with this
     * CUDA context
     */
	virtual class PxGpuDispatcher *getGpuDispatcher() = 0;

    /**
     * \brief Context manager has a valid CUDA context
     *
     * This method should be called after creating a PxCudaContextManager,
     * especially if the manager was responsible for allocating its own
     * CUDA context (desc.ctx == NULL).  If it returns false, there is
     * no point in assigning this manager's PxGpuDispatcher to a
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
	virtual bool supportsArchSM35() const = 0;  //!< GK110
	virtual bool supportsArchSM50() const = 0;  //!< GM100
	virtual bool supportsArchSM52() const = 0;  //!< GM200
	virtual bool isIntegrated() const = 0;      //!< true if GPU is an integrated (MCP) part
	virtual bool canMapHostMemory() const = 0;  //!< true if GPU map host memory to GPU (0-copy)
	virtual int  getDriverVersion() const = 0;  //!< returns cached value of cuGetDriverVersion()
	virtual size_t getDeviceTotalMemBytes() const = 0; //!< returns cached value of device memory size
	virtual int	getMultiprocessorCount() const = 0; //!< returns cache value of SM unit count
    virtual unsigned int getClockRate() const = 0; //!< returns cached value of SM clock frequency
    virtual int  getSharedMemPerBlock() const = 0; //!< returns total amount of shared memory available per block in bytes
	virtual unsigned int getMaxThreadsPerBlock() const = 0; //!< returns the maximum number of threads per block
    virtual const char *getDeviceName() const = 0; //!< returns device name retrieved from driver
	virtual PxCudaInteropMode::Enum getInteropMode() const = 0; //!< interop mode the context was created with

	virtual void setUsingConcurrentStreams(bool) = 0; //!< turn on/off using concurrent streams for GPU work
	virtual bool getUsingConcurrentStreams() const = 0; //!< true if GPU work can run in concurrent streams
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
     * \brief Release the PxCudaContextManager
     *
     * When the manager instance is released, it also releases its
     * PxGpuDispatcher instance and PxCudaMemoryManager.  Before the memory
     * manager is released, it frees all allocated memory pages.  If the
     * PxCudaContextManager created the CUDA context it was responsible
     * for, it also frees that context.
     *
     * Do not release the PxCudaContextManager if there are any scenes
     * using its PxGpuDispatcher.  Those scenes must be released first
     * since there is no safe way to remove a PxGpuDispatcher from a
     * TaskManager once the TaskManager has been given to a scene.
     *
     */
	virtual void release() = 0;

protected:

    /**
     * \brief protected destructor, use release() method
     */
    virtual ~PxCudaContextManager() {}
};

/**
 * \brief Convenience class for holding CUDA lock within a scope
 */
class PxScopedCudaLock
{
public:
    /**
     * \brief ScopedCudaLock constructor
     */
	PxScopedCudaLock(PxCudaContextManager& ctx) : mCtx(&ctx)
	{
		mCtx->acquireContext();
	}

    /**
     * \brief ScopedCudaLock destructor
     */
	~PxScopedCudaLock()
	{
		mCtx->releaseContext();
	}

protected:

    /**
     * \brief CUDA context manager pointer (initialized in the the constructor)
     */
    PxCudaContextManager* mCtx;
};

#if PX_SUPPORT_GPU_PHYSX
/**
 * \brief Ask the NVIDIA control panel which GPU has been selected for use by
 * PhysX.  Returns -1 if no PhysX capable GPU is found or GPU PhysX has
 * been disabled.
 */
int PxGetSuggestedCudaDeviceOrdinal(PxErrorCallback& errc);

/**
 * \brief Allocate a CUDA Context manager, complete with heaps and task dispatcher.
 * You only need one CUDA context manager per GPU device you intend to use for
 * CUDA tasks.  If mgr is NULL, no profiling of CUDA code will be possible.
 */
PxCudaContextManager* PxCreateCudaContextManager(PxFoundation& foundation, const PxCudaContextManagerDesc& desc, physx::PxProfileZoneManager* mgr);

/**
 * \brief get handle of physx GPU module
 */
void* PxLoadPhysxGPUModule(const char* appGUID = NULL);
#endif

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif
