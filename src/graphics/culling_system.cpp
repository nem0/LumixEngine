#include "core/lumix.h"
#include "culling_system.h"

#include "core/array.h"
#include "core/frustum.h"
#include "core/sphere.h"

#include "core/mtjd/group.h"
#include "core/mtjd/manager.h"
#include "core/mtjd/job.h"

namespace Lumix
{
	class CullingJob : public MTJD::Job
	{
	public:
		CullingJob(Array<Sphere>& spheres, Array<bool>& out, int start, int end, const Frustum& frustum, MTJD::Manager& manager)
			: Job(true, MTJD::Priority::Default, false, manager)
			, m_spheres(spheres)
			, m_result(out)
			, m_start(start)
			, m_end(end)
			, m_frustum(frustum)
		{
			setJobName("CullingJob");
		}

		virtual ~CullingJob()
		{


		}

		virtual void execute() override
		{
			int end = m_end;
			for (int i = m_start; i < end; i++)
			{
				const Sphere& sphere = m_spheres[i];
				m_result[i] = m_frustum.sphereInFrustum(sphere.m_position, sphere.m_radius);

			}
		}

	private:
		Array<Sphere>& m_spheres;
		Array<bool>& m_result;
		int m_start;
		int m_end;
		const Frustum& m_frustum;

	};


	class CullingSystemImpl : public CullingSystem
	{
	public:
		CullingSystemImpl(MTJD::Manager& mtjd_manager)
			: m_mtjd_manager(mtjd_manager)
			, m_sync_point(true)
		{
		}

		virtual ~CullingSystemImpl()
		{

		}



		virtual const Array<bool>& getResult() override
		{

			return m_result;
		}


		virtual const Array<bool>& getResultAsync() override
		{
			m_sync_point.sync();
			return m_result;
		}

		virtual void cullToFrustum(const Frustum& frustum) override
		{

			int count = m_spheres.size();
			m_result.clear();
			m_result.resize(count);

			for (int i = 0; i < count; i++)
			{
				const Sphere& sphere = m_spheres[i];
				m_result[i] = frustum.sphereInFrustum(sphere.m_position, sphere.m_radius);

			}

		}


		virtual void cullToFrustumAsync(const Frustum& frustum) override
		{
			int count = m_spheres.size();
			m_result.clear();
			m_result.resize(count);

			uint32_t cpu_count = m_mtjd_manager.getCpuThreadsCount();
			ASSERT(cpu_count > 0);

			int step = count / cpu_count;
			uint32_t i = 0;
			for (; i < cpu_count - 1; i++)
			{
				CullingJob* cj = LUMIX_NEW(CullingJob)(m_spheres, m_result, i * step, (i + 1) * step, frustum, m_mtjd_manager);
				cj->addDependency(&m_sync_point);
				m_mtjd_manager.schedule(cj);
			}

			CullingJob* cj = LUMIX_NEW(CullingJob)(m_spheres, m_result, i * step, count, frustum, m_mtjd_manager);
			cj->addDependency(&m_sync_point);
			m_mtjd_manager.schedule(cj);
		}


		virtual void addStatic(const Sphere& sphere) override
		{
			m_spheres.push(sphere);
		}


		virtual void insert(const Array<Sphere>& spheres) override
		{
			for (int i = 0; i < spheres.size(); i++)
			{
				m_spheres.push(spheres[i]);
			}
		}

	private:
		Array<Sphere> m_spheres;
		Array<bool> m_result;
		//Array <

		MTJD::Manager& m_mtjd_manager;
		MTJD::Group m_sync_point;
	};


	CullingSystem* CullingSystem::create(MTJD::Manager& mtjd_manager)
	{
		return LUMIX_NEW(CullingSystemImpl)(mtjd_manager);
	}


	void CullingSystem::destroy(CullingSystem& culling_system)
	{
		LUMIX_DELETE(&culling_system);
	}
}