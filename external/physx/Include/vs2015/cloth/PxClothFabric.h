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


#ifndef PX_PHYSICS_NX_CLOTH_FABRIC
#define PX_PHYSICS_NX_CLOTH_FABRIC
/** \addtogroup cloth
  @{
*/


#include "common/PxBase.h"

#ifndef PX_DOXYGEN
namespace physx
{
#endif

/**
\brief Describe type of phase in cloth fabric.
\see PxClothFabric for an explanation of concepts on phase and set.
*/
struct PxClothFabricPhaseType
{
	enum Enum
	{
		eINVALID,     //!< invalid type 
		eVERTICAL,    //!< resists stretching or compression, usually along the gravity
		eHORIZONTAL,  //!< resists stretching or compression, perpendicular to the gravity
		eBENDING,     //!< resists out-of-plane bending in angle-based formulation
		eSHEARING,    //!< resists in-plane shearing along (typically) diagonal edges,
        eCOUNT        // internal use only
	};
};

/**
\brief References a set of constraints that can be solved in parallel.
\see PxClothFabric for an explanation of the concepts on phase and set.
*/
struct PxClothFabricPhase
{
	PxClothFabricPhase(PxClothFabricPhaseType::Enum type = 
		PxClothFabricPhaseType::eINVALID, PxU32 index = 0);

	/**
	\brief Type of constraints to solve.
	*/
	PxClothFabricPhaseType::Enum phaseType;

	/**
	\brief Index of the set that contains the particle indices.
	*/
	PxU32 setIndex;
};

PX_INLINE PxClothFabricPhase::PxClothFabricPhase(
	PxClothFabricPhaseType::Enum type, PxU32 index)
	: phaseType(type)
	, setIndex(index)
{}

/**
\brief References all the data required to create a fabric.
\see PxPhysics.createClothFabric(), PxClothFabricCooker.getDescriptor()
*/
class PxClothFabricDesc
{
public:
	/** \brief The number of particles needed when creating a PxCloth instance from the fabric. */
	PxU32 nbParticles;

	/** \brief The number of solver phases. */
	PxU32 nbPhases;
	/** \brief Array defining which constraints to solve each phase. See #PxClothFabric.getPhases(). */
	const PxClothFabricPhase* phases;

	/** \brief The number of sets in the fabric. */
	PxU32 nbSets;
	/** \brief Array with an index per set which points one entry beyond the last constraint of the set. See #PxClothFabric.getSets(). */
	const PxU32* sets;

	/** \brief Array of particle indices which specifies the pair of constrained vertices. See #PxClothFabric.getParticleIndices(). */
	const PxU32* indices;
	/** \brief Array of rest values for each constraint. See #PxClothFabric.getRestvalues(). */
	const PxReal* restvalues;

	/** \brief Size of tetherAnchors and tetherLengths arrays, needs to be multiple of nbParticles. */
	PxU32 nbTethers;
	/** \brief Array of particle indices specifying the tether anchors. See #PxClothFabric.getTetherAnchors(). */
	const PxU32* tetherAnchors;
	/** \brief Array of rest distance between tethered particle pairs. See #PxClothFabric.getTetherLengths(). */
	const PxReal* tetherLengths;

	/**
	\brief constructor sets to default.
	*/
	PX_INLINE PxClothFabricDesc();

	/**
	\brief (re)sets the structure to the default.	
	*/
	PX_INLINE void setToDefault();

	/**
	\brief Returns true if the descriptor is valid.
	\return True if the current settings are valid
	*/
	PX_INLINE bool isValid() const;
};

PX_INLINE PxClothFabricDesc::PxClothFabricDesc()
{
	setToDefault();
}

PX_INLINE void PxClothFabricDesc::setToDefault()
{
	memset(this, 0, sizeof(PxClothFabricDesc));
}

PX_INLINE bool PxClothFabricDesc::isValid() const
{
	return nbParticles && nbPhases && phases && restvalues && nbSets 
		&& sets && indices && (!nbTethers || (tetherAnchors && tetherLengths));
}


/**
\brief A cloth fabric is a structure that contains all the internal solver constraints of a cloth mesh.
\details A fabric consists of \c phases that represent a group of internal constraints of the same type.
Each phase references an array of \c rest-values and a \c set of particle indices.
The data representation for the fabric has layers of indirect indices:
\arg All particle indices for the constraints of the fabric are stored in one linear array and referenced by the sets.
\arg Each constraint (particle index pair) has one entry in the restvalues array.
\arg The set array holds the prefix sum of the number of constraints per set and is referenced by the phases.
\arg A phase consists of the type of constraints, the index of the set referencing the indices.

Additionally, a fabric also stores the data for the tether constraints, which limit the distances 
between two particles. The tether constraints are stored in an array, and the index of a constraint
determines which particle is affected: element i affects particle i%N, where N is the number of particles.
The tether anchor is the index of the other particle, and the tether length is the maximum distance that
these two particles are allowed to be away from each other. A tether constraint is momentum conserving 
if the anchor particle has infinite mass (zero inverse weight).

@see The fabric structure can be created from a mesh using PxClothFabricCreate. Alternatively, the fabric data can 
be saved into a stream (see PxClothFabricCooker.save()) and later created from the stream using PxPhysics.createClothFabric(PxInputStream&).
*/
class PxClothFabric	: public PxBase
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
    \brief Get number of particle indices.
	\return The size of the particle indices array.
    */
	virtual	PxU32 getNbParticleIndices() const = 0;

    /**
    \brief Get number of tether constraints.
	\return The size of the tether anchors and lengths arrays.
    */
	virtual PxU32 getNbTethers() const = 0;

    /**
    \brief Copies the phase array to a user specified buffer.
	\details The phase array is a mapping of the phase index to the corresponding phase.
	The array has the same length as getNbPhases().
    \param [in] userPhaseBuffer Destination buffer to copy the phase data to.
    \param [in] bufferSize Size of userPhaseBuffer, should be at least getNbPhases().
    \return getNbPhases() if the copy was successful, 0 otherwise.
	\note This function is potentially slow. Consider caching 
	the (static) result instead of retrieving it multiple times.
    */    
    virtual PxU32 getPhases(PxClothFabricPhase* userPhaseBuffer, PxU32 bufferSize) const = 0;

    /**
	\brief Copies the set array to a user specified buffer.
	\details The set array is the inclusive prefix sum of the number of constraints per set.
	It has the same length as getNbSets().
	\param [in] userSetBuffer Destination buffer to copy the set data to.
	\param [in] bufferSize Size of userSetBuffer, should be at least getNbSets().
	\return getNbSets() if the copy was successful, 0 otherwise.
	\note Indices of the i-th set are stored at indices [i?2*set[i-1]:0, 2*set[i]) in the indices array.
	\note This function is potentially slow. Consider caching 
	the (static) result instead of retrieving it multiple times.
    */    
    virtual PxU32 getSets(PxU32* userSetBuffer, PxU32 bufferSize) const = 0;

    /**
	\brief Copies the particle indices array to a user specified buffer.
	\details The particle indices array determines which particles are affected by each constraint.
	It has the same length as getNbParticleIndices() and twice the number of constraints.
	\param [in] userParticleIndexBuffer Destination buffer to copy the set data to.
	\param [in] bufferSize Size of userParticleIndexBuffer, should be at least getNbParticleIndices().
	\return getNbParticleIndices() if the copy was successful, 0 otherwise.
	\note This function is potentially slow. Consider caching 
	the (static) result instead of retrieving it multiple times.
    */
    virtual PxU32 getParticleIndices(PxU32* userParticleIndexBuffer, PxU32 bufferSize) const = 0;

    /**
	\brief Copies the rest values array to a user specified buffer.
	\details The rest values array holds the target value of the constraint in rest state, 
	for example edge length for stretch constraints.
	It stores one value per constraint, so its length is half of getNbParticleIndices(), and 
	it has the same length as getNbRestvalues(). The rest-values are stored in the order
	they are (indirectly) referenced by the phases. 
	\param [in] userRestvalueBuffer Destination buffer to copy the set data to.
	\param [in] bufferSize Size of userRestvalueBuffer, should be at least getNbRestvalues().
	\return getNbRestvalues() if the copy was successful, 0 otherwise.
	\note This function is potentially slow. Between calling scaleRestlengths(), 
	consider caching the result instead of retrieving it multiple times.
    */
	virtual PxU32 getRestvalues(PxReal* userRestvalueBuffer, PxU32 bufferSize) const = 0;

    /**
	\brief Copies the tether anchors array to a user specified buffer.
	\details The tether anchors array stores for each particle the index of 
	another particle from which it cannot move further away than specified by the 
	tether lengths array. 
	\param [in] userAnchorBuffer Destination buffer to copy the set data to.
	\param [in] bufferSize Size of userAnchorBuffer, should be at least getNbTethers().
	\return getNbTethers() if the copy was successful, 0 otherwise.
	\note This function is potentially slow, consider caching the 
	result instead of retrieving the data multiple times.
	\see getTetherLengths, getNbTethers
    */
	virtual PxU32 getTetherAnchors(PxU32* userAnchorBuffer, PxU32 bufferSize) const = 0;

    /**
	\brief Copies the tether lengths array to a user specified buffer.
	\details The tether lengths array stores for each particle how far it is 
	allowed to move away from the particle specified by the tether anchor array. 
	\param [in] userLengthBuffer Destination buffer to copy the set data to.
	\param [in] bufferSize Size of userLengthBuffer, should be at least getNbTethers().
	\return getNbTethers() if the copy was successful, 0 otherwise.
	\note This function is potentially slow. Between calling scaleRestlengths(), 
	consider caching the result instead of retrieving it multiple times.
	\see getTetherAnchors, getNbTethers
    */
	virtual PxU32 getTetherLengths(PxReal* userLengthBuffer, PxU32 bufferSize) const = 0;

	/**
	\deprecated
	\brief Returns the type of a phase.
	\param [in] phaseIndex The index of the phase to return the type for.
	\return The phase type as PxClothFabricPhaseType::Enum.
    \note If phase index is invalid, PxClothFabricPhaseType::eINVALID is returned.
    */
	PX_DEPRECATED virtual PxClothFabricPhaseType::Enum getPhaseType(PxU32 phaseIndex) const = 0;

    /**
    \brief Scale all rest values of length dependent constraints.
    \param[in] scale The scale factor to multiply each rest value with.
    */
	virtual	void scaleRestlengths(PxReal scale) = 0;

	/**
	\brief Reference count of the cloth instance
	\details At creation, the reference count of the fabric is 1. Every cloth instance referencing this fabric increments the
	count by 1.	When the reference count reaches 0, and only then, the fabric gets released automatically.
	\see PxCloth
	*/
	virtual	PxU32 getReferenceCount() const = 0;

	virtual	const char*	getConcreteTypeName() const	{ return "PxClothFabric";	}

protected:

	PX_INLINE PxClothFabric(PxType concreteType, PxBaseFlags baseFlags) : PxBase(concreteType, baseFlags) {}
	PX_INLINE PxClothFabric(PxBaseFlags baseFlags) : PxBase(baseFlags) {}
	virtual	~PxClothFabric() {}
	virtual	bool isKindOf(const char* name) const { return !strcmp("PxClothFabric", name) || PxBase::isKindOf(name); }
};

#ifndef PX_DOXYGEN
} // namespace physx
#endif

/** @} */
#endif
