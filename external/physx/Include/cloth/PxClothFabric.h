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
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#ifndef PX_PHYSICS_NX_CLOTH_FABRIC
#define PX_PHYSICS_NX_CLOTH_FABRIC
/** \addtogroup cloth
  @{
*/


#include "common/PxSerialFramework.h"
#include "cloth/PxClothFabricTypes.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief A cloth fabric is a structure that contains all the internal solver constraints of a clothing mesh.
\details A fabric consists of \c phases that represent a group of internal constraints of the same type.
Each phase references an array of \c rest-values and a \c set of particle indices, grouped into \c fibers.
A fiber is a linear array of particle indices that are connected by constraints, and fibers of a set are 
guaranteed to be disconnected so they can be solved in parallel.\n
The data representation for the fabric has layers of indirect indices:
\arg All particle indices of the fabric are stored in one linear array and reference by the fibers.
\arg The fiber array holds the prefix sum of the number of indices per fiber and is referenced by the sets.
\arg The set array holds the prefix sum of the number of fibers per set and is referenced by the phases.
\arg A phase consists of the type of constraints, and the index of the set referencing fibers and indices.
\arg The rest-value are packed in a linear array in the order they are referenced by the phases.

Here is an example of a simple fabric structure with 3 fiber stretch set and a 2 fiber bending set:
\verbatim
phase types: [                    eVERTICAL,           eBENDING]
phases:      [                            0,                  1]
sets:        [                            3,                  5]
             |----------- set 0 -----------|------ set 1 ------|
fibers:      [        3,        5,        7,       10,       13] 
             | fiber 0 | fiber 1 | fiber 2 | fiber 3 | fiber 4 |
indices:     [2,  0,  3,   6,   4,   5,   1,  4, 2, 0,  1, 3, 6] 
restvalues:  [2.0,  2.0,   2.0   ,   2.0   ,   1.0   ,   1.0   ]
\endverbatim
\see The fabric structure can be created from a mesh using PxCooking.cookClothFabric() and saved to a PxStream.
Instances of PxClothFabric can then be created from the stream using PxPhysics.createClothFabric().
*/
class PxClothFabric	: public PxSerializable
{
public:
	/**
	\brief Release the cloth fabric.
	\details Releases the application's reference to the cloth fabric.
	The fabric is destroyed when the application's reference is released and all cloth instances referencing the fabric are destroyed.
	\see PxPhysics.createClothFabric()
	*/
	virtual void release() = 0;

	/**
    \brief Returns number of particles.
    \return The number of particles needed when creating a PxCloth instance from the fabric.
    */ 
	virtual	PxU32 getNbParticles() const = 0;

	/**
    \brief Returns number of phases.
    \return The number of solver phases. 
    */ 
	virtual PxU32 getNbPhases() const = 0;

	/**
    \brief Returns number of rest values.
    \return The size of the rest values array.
    */ 
	virtual PxU32 getNbRestvalues() const = 0;

    /**
    \brief Returns number of sets.
    \return The size of the set array.
    */ 
	virtual	PxU32 getNbSets() const = 0;

    /**
    \brief Returns number of fibers.
    \return The size of the fiber array.
    */
	virtual	PxU32 getNbFibers() const = 0;

    /**
    \brief Get number of particle indices.
	\return The size of the particle indices array.
    */
	virtual	PxU32 getNbParticleIndices() const = 0;

    /**
    \brief Copies the phase array to a user specified buffer.
	\details The phase array is a mapping of the phase index to the corresponding set index.
	A set index can occur multiple times. The array has the same length as getNbPhases().
    \param [in] userPhaseBuffer Destination buffer to copy the phase data to.
    \param [in] bufferSize Size of userPhaseBuffer, should be at least getNbPhases().
    \return getNbPhases() if the copy was successful, 0 otherwise.
    */    
    virtual PxU32 getPhases(PxU32* userPhaseBuffer, PxU32 bufferSize) const = 0;

    /**
	\brief Copies the set array to a user specified buffer.
	\details The set array is the inclusive prefix sum of the number of fibers per set.
	It has the same length as getNbSets().
	\param [in] userSetBuffer Destination buffer to copy the set data to.
	\param [in] bufferSize Size of userSetBuffer, should be at least getNbSets().
	\return getNbSets() if the copy was successful, 0 otherwise.
	\note Fibers of the i-th set are stored at indices [i?set[i-1]:0, set[i]) in the fibers array.
    */    
    virtual PxU32 getSets(PxU32* userSetBuffer, PxU32 bufferSize) const = 0;

    /**
	\brief Copies the fibers array to a user specified buffer.
	\details The fibers array is the inclusive prefix sum of the number of particle indices per set.
	It has the same length as getNbFibers().
	\param [in] userFiberBuffer Destination buffer to copy the set data to.
	\param [in] bufferSize Size of userFiberBuffer, should be at least getNbFibers().
	\return getNbFibers() if the copy was successful, 0 otherwise.
	\note Particle indices of the i-th fiber are stored at indices [i?fiber[i-1]:0, fiber[i]) in the particle indices array.
    */    
    virtual PxU32 getFibers(PxU32* userFiberBuffer, PxU32 bufferSize) const = 0;

    /**
	\brief Copies the particle indices array to a user specified buffer.
	\details The particle indices array determines which particles are affected by each constraint.
	It has the same length as getNbParticleIndices().
	\param [in] userParticleIndexBuffer Destination buffer to copy the set data to.
	\param [in] bufferSize Size of userParticleIndexBuffer, should be at least getNbParticleIndices().
	\return getNbParticleIndices() if the copy was successful, 0 otherwise.
    */
    virtual PxU32 getParticleIndices(PxU32* userParticleIndexBuffer, PxU32 bufferSize) const = 0;

    /**
	\brief Copies the rest values array to a user specified buffer.
	\details The rest values array holds the target value of the constraint in rest state, 
	for example edge length for stretch constraints or bending angle for bending constraints.
	It has the same length as getNbRestvalues(). The rest-values are stored in the order
	they are (indirectly) referenced by the phases. There is one less rest value than 
	particle indices in a stretch fiber, and two less in a bending fiber. 
	\param [in] userRestvalueBuffer Destination buffer to copy the set data to.
	\param [in] bufferSize Size of userRestvalueBuffer, should be at least getNbRestvalues().
	\return getNbRestvalues() if the copy was successful, 0 otherwise.
    */
	virtual PxU32 getRestvalues(PxReal* userRestvalueBuffer, PxU32 bufferSize) const = 0;


	/**
	\brief Returns the type of a phase.
	\param [in] phaseIndex The index of the phase to return the type for.
	\return The phase type as PxClothFabricPhaseType::Enum.
    \note If phase index is invalid, PxClothFabricPhaseType::eINVALID is returned.
    */
	virtual	PxClothFabricPhaseType::Enum getPhaseType(PxU32 phaseIndex) const = 0;

    /**
    \brief Scale all rest values of a phase type.
    \param[in] phaseType Type of phases to scale rest values for.
    \param[in] scale The scale factor to multiply each rest value with.
	\note Only call this function when no PxCloth instance has been created for this fabric yet.
	\note Scaling rest values of type PxClothFabricPhaseType::eBENDING results in undefined behavior.
    */
	virtual	void scaleRestvalues(PxClothFabricPhaseType::Enum phaseType, PxReal scale) = 0;

	/**
	\brief Reference count of the cloth instance
	\details At creation, the reference count of the fabric is 1. Every cloth instance referencing this fabric increments the
	count by 1.	When the reference count reaches 0, and only then, the fabric gets released automatically.
	\see PxCloth
	*/
	virtual	PxU32 getReferenceCount() const = 0;

	virtual		const char*		getConcreteTypeName() const					{	return "PxClothFabric";	}
protected:

	PxClothFabric()										{}
	PxClothFabric(PxRefResolver& v)	: PxSerializable(v)	{}
	virtual						~PxClothFabric() {}
	virtual		bool			isKindOf(const char* name)	const		{	return !strcmp("PxClothFabric", name) || PxSerializable::isKindOf(name); }
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
