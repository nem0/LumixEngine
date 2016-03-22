#include "pose.h"

#include "core/matrix.h"
#include "core/math_utils.h"


namespace Lumix
{


void Pose::setMatrix(Matrix& mtx) const
{
	m_rotation.toMatrix(mtx);
	mtx.translate(m_position);
}


void Pose::blend(Pose& rhs, float weight)
{
	if (weight <= 0.001f)
		return;

	weight = Math::clamp(weight, 0.0f, 1.0f);
	float inv = 1.0f - weight;

	lerp(m_position, rhs.m_position, &m_position, weight);
	nlerp(m_rotation, rhs.m_rotation, &m_rotation, weight);
}


} // namespace Lumix
