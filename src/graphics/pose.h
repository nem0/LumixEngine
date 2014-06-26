#pragma once


#include "core/lumix.h"


namespace Lumix
{


struct Matrix;
struct Quat;
struct Vec3;


class Pose
{
	public:
		Pose();
		~Pose();

		void resize(int count);
		void setMatrices(Matrix* mtx) const;
		int getCount() const { return m_count; }
		Vec3* getPositions() const { return m_positions; }
		Quat* getRotations() const { return m_rotations; }

	private:
		Pose(const Pose&) {}
		void operator =(const Pose&) {}

	private:
		int32_t m_count;
		Vec3* m_positions;
		Quat* m_rotations;
};


} // ~namespace Lumix
