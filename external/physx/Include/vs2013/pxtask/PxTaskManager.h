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

#ifndef PX_TASK_MANAGER_H
#define PX_TASK_MANAGER_H

#include "foundation/Px.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxProfileZoneManager;

PX_PUSH_PACK_DEFAULT

class PxBaseTask;
class PxTask;
class PxLightCpuTask;
class PxSpuTask;
typedef unsigned long PxTaskID;

/**
\brief Identifies the type of each heavyweight PxTask object

\note This enum type is only used by PxTask and GpuTask objects, LightCpuTasks do not use this enum.

@see PxTask
@see PxLightCpuTask
*/
struct PxTaskType
{
	/**
	 * \brief Identifies the type of each heavyweight PxTask object
	 */
	enum Enum
	{
		TT_CPU,				//!< PxTask will be run on the CPU
		TT_GPU,				//!< PxTask will be run on the GPU
		TT_NOT_PRESENT,		//!< Return code when attempting to find a task that does not exist
		TT_COMPLETED		//!< PxTask execution has been completed
	};
};

class PxCpuDispatcher;
class PxSpuDispatcher;
class PxGpuDispatcher;

/** 
 \brief The PxTaskManager interface
 
 A PxTaskManager instance holds references to user-provided dispatcher objects, when tasks are
 submitted the PxTaskManager routes them to the appropriate dispatcher and handles task profiling if enabled. 
 Users should not implement the PxTaskManager interface, the SDK creates it's own concrete PxTaskManager object
 per-scene which users can configure by passing dispatcher objects into the PxSceneDesc.

 @see PxSceneDesc
 @see CpuDispatcher
 @see PxGpuDispatcher
 @see PxSpuDispatcher
*/
class PxTaskManager
{
public:

	/**
	\brief Set the user-provided dispatcher object for CPU tasks

	\param[in] ref The dispatcher object.

	@see CpuDispatcher
	*/
	virtual void     setCpuDispatcher(PxCpuDispatcher& ref) = 0;

	/**
	\brief Set the user-provided dispatcher object for GPU tasks

	\param[in] ref The dispatcher object.

	@see PxGpuDispatcher
	*/
	virtual void     setGpuDispatcher(PxGpuDispatcher& ref) = 0;
	
	/**
	\brief Set the user-provided dispatcher object for SPU tasks

	\param[in] ref The dispatcher object.

	@see PxSpuDispatcher
	*/
    virtual void     setSpuDispatcher(PxSpuDispatcher& ref) = 0;

	/**
	\brief Set profile zone used for task profiling.

	\param[in] ref The profile zone manager

	@see PxProfileZoneManager
	*/
	virtual void     initializeProfiling(PxProfileZoneManager& ref) = 0;

	/**
	\brief Get the user-provided dispatcher object for CPU tasks

	\return The CPU dispatcher object.

	@see CpuDispatcher
	*/
	virtual PxCpuDispatcher*			getCpuDispatcher() const = 0;

	/**
	\brief Get the user-provided dispatcher object for GPU tasks

	\return The GPU dispatcher object.

	@see PxGpuDispatcher
	*/
	virtual PxGpuDispatcher*			getGpuDispatcher() const = 0;

	/**
	\brief Get the user-provided dispatcher object for SPU tasks

	\return The SPU dispatcher object.

	@see PxSpuDispatcher
	*/
	virtual PxSpuDispatcher*			getSpuDispatcher() const = 0;

	/**
	\brief Reset any dependencies between Tasks

	\note Will be called at the start of every frame before tasks are submited.

	@see PxTask
	*/
	virtual void	resetDependencies() = 0;
	
	/**
	\brief Called by the owning scene to start the task graph.

	\note All tasks with with ref count of 1 will be dispatched.

	@see PxTask
	*/
	virtual void	startSimulation() = 0;

	/**
	\brief Called by the owning scene at the end of a simulation step to synchronize the PxGpuDispatcher

	@see PxGpuDispatcher
	*/
	virtual void	stopSimulation() = 0;

	/**
	\brief Called by the worker threads to inform the PxTaskManager that a task has completed processing

	\param[in] task The task which has been completed
	*/
	virtual void	taskCompleted(PxTask& task) = 0;

	/**
	\brief Retrieve a task by name

	\param[in] name The unique name of a task
	\return The ID of the task with that name, or TT_NOT_PRESENT if not found
	*/
	virtual PxTaskID  getNamedTask(const char* name) = 0;

	/**
	\brief Submit a task with a unique name.

	\param[in] task The task to be executed
	\param[in] name The unique name of a task
	\param[in] type The type of the task (default TT_CPU)
	\return The ID of the task with that name, or TT_NOT_PRESENT if not found

	*/
	virtual PxTaskID  submitNamedTask(PxTask* task, const char* name, PxTaskType::Enum type = PxTaskType::TT_CPU) = 0;

	/**
	\brief Submit an unnamed task.

	\param[in] task The task to be executed
	\param[in] type The type of the task (default TT_CPU)

	\return The ID of the task with that name, or TT_NOT_PRESENT if not found
	*/
	virtual PxTaskID  submitUnnamedTask(PxTask& task, PxTaskType::Enum type = PxTaskType::TT_CPU) = 0;

	/**
	\brief Retrive a task given a task ID

	\param[in] id The ID of the task to return, a valid ID must be passed or results are undefined

	\return The task associated with the ID
	*/
	virtual PxTask*   getTaskFromID(PxTaskID id) = 0;

	/**
	\brief Release the PxTaskManager object, referneced dispatchers will not be released
	*/
	virtual void        release() = 0;

	/**
	\brief Construct a new PxTaskManager instance with the given [optional] dispatchers
	*/
	static PxTaskManager* createTaskManager(PxCpuDispatcher* = 0, PxGpuDispatcher* = 0, PxSpuDispatcher* = 0);
	
protected:
	virtual ~PxTaskManager() {}

	/*! \cond PRIVATE */

	virtual void finishBefore(PxTask& task, PxTaskID taskID) = 0;
	virtual void startAfter(PxTask& task, PxTaskID taskID) = 0;

	virtual void addReference(PxTaskID taskID) = 0;
	virtual void decrReference(PxTaskID taskID) = 0;
	virtual PxI32 getReference(PxTaskID taskID) const = 0;

	virtual void decrReference(PxLightCpuTask&) = 0;
	virtual void addReference(PxLightCpuTask&) = 0;

	virtual void decrReference(PxSpuTask& spuTask) = 0;

	virtual void emitStartEvent(PxBaseTask&, PxU32 threadId=0) = 0;
	virtual void emitStopEvent(PxBaseTask&, PxU32 threadId=0) = 0;

	/*! \endcond */

	friend class PxBaseTask;
	friend class PxTask;
	friend class PxLightCpuTask;
	friend class PxSpuTask;
	friend class PxGpuWorkerThread;
};

PX_POP_PACK

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif
