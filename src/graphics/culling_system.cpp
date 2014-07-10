#include "core/lux.h"
#include "culling_system.h"

#include "core/array.h"
#include "core/frustum.h"
#include "core/sphere.h"

namespace Lux
{
	
	struct CullingSystemImpl
	{
		Array<Sphere> spheres;
		Array<bool> result;
	};

	CullingSystem::~CullingSystem()
	{
		ASSERT(NULL == m_impl);
	}


	void CullingSystem::create()
	{
		ASSERT(NULL == m_impl);
		m_impl = LUX_NEW(CullingSystemImpl);
	}


	void CullingSystem::destroy()
	{
		LUX_DELETE(m_impl);
		m_impl = NULL;
	}

	const Array<bool>& CullingSystem::getResult()
	{
		return m_impl->result;
	}

	void CullingSystem::cullToFrustum(const Frustum& frustum)
	{
		int count = m_impl->spheres.size();
		m_impl->result.clear();
		m_impl->result.resize(count);

		for (int i = 0; i < count; i++)
		{
			const Sphere& sphere = m_impl->spheres[i];
			m_impl->result[i] = frustum.sphereInFrustum(sphere.m_position, sphere.m_radius);
		}
	}

	void CullingSystem::insert(const Array<Sphere>& spheres)
	{
		for (int i = 0; i < spheres.size(); i++)
		{
			m_impl->spheres.push(spheres[i]);
		}
	}


}