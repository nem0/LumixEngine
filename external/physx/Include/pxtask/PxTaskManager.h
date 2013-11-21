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

#ifndef PX_TASK_MANAGER_H
#define PX_TASK_MANAGER_H

#include "foundation/Px.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

class PxProfileZoneManager;

namespace pxtask
{

PX_PUSH_PACK_DEFAULT

class BaseTask;
class Task;
class LightCpuTask;
class SpuTask;
typedef unsigned long TaskID;

/**
\brief Identifies the type of each heavyweight Task object

\note This enum type is only used by Task and GpuTask objects, LightCpuTasks do not use this enum.

@see Task
@see LightCpuTask
*/
struct TaskType
{
	/**
	 * \brief Identifies the type of each heavyweight Task object
	 */
	enum Enum
	{
		TT_CPU,				//!< Task will be run on the CPU
		TT_GPU,				//!< Task will be run on the GPU
		TT_NOT_PRESENT,		//!< Return code when attempting to find a task that does not exist
		TT_COMPLETED		//!< Task execution has been completed
	};
};

class CpuDispatcher;
class SpuDispatcher;
class GpuDispatcher;

/** 
 \brief The TaskManager interface
 
 A TaskManager instance holds references to user-provided dispatcher objects, when tasks are
 submitted the TaskManager routes them to the appropriate dispatcher and handles task profiling if enabled. 
 Users should not implement the TaskManager interface, the SDK creates it's own concrete TaskManager object
 per-scene which users can configure by passing dispatcher objects into the PxSceneDesc.

 @see PxSceneDesc
 @see CpuDispatcher
 @see GpuDispatcher
 @see SpuDispatcher
*/
class TaskManager
{
public:

	/**
	\brief Set the user-provided dispatcher object for CPU tasks

	\param[in] ref The dispatcher object.

	@see CpuDispatcher
	*/
	virtual void     setCpuDispatcher(CpuDispatcher& ref) = 0;

	/**
	\brief Set the user-provided dispatcher object for GPU tasks

	\param[in] ref The dispatcher object.

	@see GpuDispatcher
	*/
	virtual void     setGpuDispatcher(GpuDispatcher& ref) = 0;
	
	/**
	\brief Set the user-provided dispatcher object for SPU tasks

	\param[in] ref The dispatcher object.

	@see SpuDispatcher
	*/
    virtual void     setSpuDispatcher(SpuDispatcher& ref) = 0;

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
	virtual CpuDispatcher*			getCpuDispatcher() const = 0;

	/**
	\brief Get the user-provided dispatcher object for GPU tasks

	\return The GPU dispatcher object.

	@see GpuDispatcher
	*/
	virtual GpuDispatcher*			getGpuDispatcher() const = 0;

	/**
	\brief Get the user-provided dispatcher object for SPU tasks

	\return The SPU dispatcher object.

	@see SpuDispatcher
	*/
	virtual SpuDispatcher*			getSpuDispatcher() const = 0;

	/**
	\brief Reset any dependencies between Tasks

	\note Will be called at the start of every frame before tasks are submited.

	@see Task
	*/
	virtual void	resetDependencies() = 0;
	
	/**
	\brief Called by the owning scene to start the task graph.

	\note All tasks with with ref count of 1 will be dispatched.

	@see Task
	*/
	virtual void	startSimulation() = 0;

	/**
	\brief Called by the owning scene at the end of a simulation step to synchronize the GpuDispatcher

	@see GpuDispatcher
	*/
	virtual void	stopSimulation() = 0;

	/**
	\brief Called by the worker threads to inform the TaskManager that a task has completed processing

	\param[in] task The task which has been completed
	*/
	virtual void	taskCompleted(Task& task) = 0;

	/**
	\brief Retrieve a task by name

	\param[in] name The unique name of a task
	\return The ID of the task with that name, or TT_NOT_PRESENT if not found
	*/
	virtual TaskID  getNamedTask(const char* name) = 0;

	/**
	\brief Submit a task with a unique name.

	\param[in] task The task to be executed
	\param[in] name The unique name of a task
	\param[in] type The type of the task (default TT_CPU)
	\return The ID of the task with that name, or TT_NOT_PRESENT if not found

	*/
	virtual TaskID  submitNamedTask(Task* task, const char* name, TaskType::Enum type = TaskType::TT_CPU) = 0;

	/**
	\brief Submit an unnamed task.

	\param[in] task The task to be executed
	\param[in] type The type of the task (default TT_CPU)

	\return The ID of the task with that name, or TT_NOT_PRESENT if not found
	*/
	virtual TaskID  submitUnnamedTask(Task& task, TaskType::Enum type = TaskType::TT_CPU) = 0;

	/**
	\brief Retrive a task given a task ID

	\param[in] id The ID of the task to return, a valid ID must be passed or results are undefined

	\return The task associated with the ID
	*/
	virtual Task*   getTaskFromID(TaskID id) = 0;

	/**
	\brief Release the TaskManager object, referneced dispatchers will not be released
	*/
	virtual void        release() = 0;

	/**
	\brief Construct a new TaskManager instance with the given [optional] dispatchers
	*/
	static TaskManager* createTaskManager(CpuDispatcher* = 0, GpuDispatcher* = 0, SpuDispatcher* = 0);
	
protected:
	virtual ~TaskManager() {}

	/*! \cond PRIVATE */

	virtual void finishBefore(Task& task, TaskID taskID) = 0;
	virtual void startAfter(Task& task, TaskID taskID) = 0;

	virtual void addReference(TaskID taskID) = 0;
	virtual void decrReference(TaskID taskID) = 0;
	virtual PxI32 getReference(TaskID taskID) const = 0;

	virtual void decrReference(LightCpuTask&) = 0;
	virtual void addReference(LightCpuTask&) = 0;

	virtual void decrReference(SpuTask& spuTask) = 0;

	virtual void emitStartEvent(BaseTask&) = 0;
	virtual void emitStopEvent(BaseTask&) = 0;

	/*! \endcond */

	friend class BaseTask;
	friend class Task;
	friend class LightCpuTask;
	friend class SpuTask;
	friend class GpuWorkerThread;
};

PX_POP_PACK

} // end pxtask namespace

#ifndef PX_DOXYGEN
} // end physx namespace
#endif

#endif
