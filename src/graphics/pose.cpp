#include "graphics/pose.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/vec3.h"
#include "graphics/model.h"


namespace Lumix
{


Pose::Pose()
{
	m_positions = 0;
	m_rotations = 0;
	m_count = 0;
	m_is_absolute = false;
}


Pose::~Pose()
{
	LUMIX_DELETE_ARRAY(m_positions);
	LUMIX_DELETE_ARRAY(m_rotations);
}


void Pose::resize(int count)
{
	LUMIX_DELETE_ARRAY(m_positions);
	LUMIX_DELETE_ARRAY(m_rotations);
	m_count = count;
	if(m_count)
	{
		m_positions = LUMIX_NEW_ARRAY(Vec3, count);
		m_rotations = LUMIX_NEW_ARRAY(Quat, count);
	}
	else
	{
		m_positions = NULL;
		m_rotations = NULL;
	}
}

void Pose::computeAbsolute(Model& model, int i, bool* valid)
{
	if (!valid[i])
	{ 
		int parent = model.getBone(i).parent_idx;
		if (parent >= 0)
		{
			if (!valid[parent])
			{
				computeAbsolute(model, parent, valid);
			}
			m_positions[i] = m_rotations[parent] * m_positions[i] + m_positions[parent];
			m_rotations[i] = m_rotations[i] * m_rotations[parent];
		}
		valid[i] = true;
	}
}

void Pose::computeAbsolute(Model& model)
{
	/// TODO remove recursion
	if(!m_is_absolute)
	{
		ASSERT(m_count < 256);
		bool valid[256];
		memset(valid, 0, sizeof(bool) * m_count);
		for (int i = 0; i < m_count; ++i)
		{
			computeAbsolute(model, i, valid);
		}
		m_is_absolute = true;
	}
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
