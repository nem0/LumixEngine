#include "renderer/pose.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/profiler.h"
#include "core/vec.h"
#include "renderer/model.h"


namespace Lumix
{


Pose::Pose(IAllocator& allocator)
	: m_allocator(allocator)
{
	m_positions = 0;
	m_rotations = 0;
	m_count = 0;
	m_is_absolute = false;
}


Pose::~Pose()
{
	m_allocator.deallocate(m_positions);
	m_allocator.deallocate(m_rotations);
}


void Pose::blend(Pose& rhs, float weight)
{
	ASSERT(m_count == rhs.m_count);
	if (weight <= 0.001f)
	{
		return;
	}
	weight = Math::clamp(weight, 0.0f, 1.0f);
	float inv = 1.0f - weight;
	for (int i = 0, c = m_count; i < c; ++i)
	{
		m_positions[i] = m_positions[i] * inv + rhs.m_positions[i] * weight;
		nlerp(m_rotations[i], rhs.m_rotations[i], &m_rotations[i], weight);
	}
}


void Pose::resize(int count)
{
	m_is_absolute = false;
	m_allocator.deallocate(m_positions);
	m_allocator.deallocate(m_rotations);
	m_count = count;
	if(m_count)
	{
		m_positions = static_cast<Vec3*>(m_allocator.allocate(sizeof(Vec3) * count));
		m_rotations = static_cast<Quat*>(m_allocator.allocate(sizeof(Quat) * count));
	}
	else
	{
		m_positions = nullptr;
		m_rotations = nullptr;
	}
}


void Pose::computeAbsolute(Model& model)
{
	PROFILE_FUNCTION();
	if (m_is_absolute) return;
	for (int i = model.getFirstNonrootBoneIndex(); i < m_count; ++i)
	{
		int parent = model.getBone(i).parent_idx;
		m_positions[i] = m_rotations[parent] * m_positions[i] + m_positions[parent];
		m_rotations[i] = m_rotations[i] * m_rotations[parent];
	}
	m_is_absolute = true;
}


void Pose::computeRelative(Model& model)
{
	PROFILE_FUNCTION();
	if (!m_is_absolute) return;
	for (int i = m_count - 1; i >= model.getFirstNonrootBoneIndex(); --i)
	{
		int parent = model.getBone(i).parent_idx;
		m_positions[i] = -m_rotations[parent] * (m_positions[i] - m_positions[parent]);
		m_rotations[i] = m_rotations[i] * -m_rotations[parent];
	}
	m_is_absolute = false;
}


void Pose::setMatrices(Matrix* mtx) const
{
	for(int i = 0, c = m_count; i < c; ++i)
	{
		m_rotations[i].toMatrix(mtx[i]);
		mtx[i].translate(m_positions[i]);
	}
}


} // ~namespace Lumix
