#pragma once


#include "lumix.h"


namespace Lumix
{


class IAllocator;
struct Matrix;
class Model;
struct Quat;
struct Vec3;


class LUMIX_RENDERER_API Pose
{
	public:
		explicit Pose(IAllocator& allocator);
		~Pose();

		void resize(int count);
		void setMatrices(Matrix* mtx) const;
		int getCount() const { return m_count; }
		Vec3* getPositions() const { return m_positions; }
		Quat* getRotations() const { return m_rotations; }
		void computeAbsolute(Model& model);
		void computeRelative(Model& model);
		void setIsRelative() { m_is_absolute = false; }
		void setIsAbsolute() { m_is_absolute = true; }
		void blend(Pose& rhs, float weight);

	private:
		Pose(const Pose&);
		void operator =(const Pose&);
		
	private:
		IAllocator& m_allocator;
		bool m_is_absolute;
		int32 m_count;
		Vec3* m_positions;
		Quat* m_rotations;
};


} // ~namespace Lumix
