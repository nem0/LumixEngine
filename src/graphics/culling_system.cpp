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
	typedef Array<bool> VisibilityFlags;

	static const int MIN_ENTITIES_PER_THREAD = 50;

	static void doCulling(
		int start_index,
		const Sphere* LUMIX_RESTRICT start,
		const Sphere* LUMIX_RESTRICT end,
		const bool* LUMIX_RESTRICT visiblity_flags,
		const Frustum* LUMIX_RESTRICT frustum,
		CullingSystem::Subresults& results
		)
	{
		int i = start_index;
		ASSERT(results.empty());
		for (const Sphere* sphere = start; sphere <= end; sphere++, ++i)
		{
			if (frustum->isSphereInside(sphere->m_position, sphere->m_radius) && visiblity_flags[i])
			{
				results.push(i);
			}
		}
	}

	class CullingJob : public MTJD::Job
	{
	public:
		CullingJob(const CullingSystem::InputSpheres& spheres, VisibilityFlags& visibility_flags, CullingSystem::Subresults& results, int start, int end, const Frustum& frustum, MTJD::Manager& manager, IAllocator& allocator)
			: Job(true, MTJD::Priority::Default, false, manager, allocator)
			, m_spheres(spheres)
			, m_results(results)
			, m_start(start)
			, m_end(end)
			, m_frustum(frustum)
			, m_visibility_flags(visibility_flags)
		{
			setJobName("CullingJob");
			m_results.reserve(end - start);
			ASSERT(m_results.empty());
			m_is_executed = false;
		}

		virtual ~CullingJob()
		{
		}

		virtual void execute() override
		{
			ASSERT(m_results.empty() && !m_is_executed);
			doCulling(m_start, &m_spheres[m_start], &m_spheres[m_end], &m_visibility_flags[0], &m_frustum, m_results);
			m_is_executed = true;
		}

	private:
		const CullingSystem::InputSpheres& m_spheres;
		CullingSystem::Subresults& m_results;
		VisibilityFlags& m_visibility_flags;
		int m_start;
		int m_end;
		const Frustum& m_frustum;
		bool m_is_executed;

	};

	class CullingSystemImpl : public CullingSystem
	{
	public:
		CullingSystemImpl(MTJD::Manager& mtjd_manager, IAllocator& allocator)
			: m_mtjd_manager(mtjd_manager)
			, m_sync_point(true, m_allocator)
			, m_allocator(allocator)
			, m_spheres(m_allocator)
			, m_result(m_allocator)
			, m_visibility_flags(m_allocator)
		{
			m_result.emplace(m_allocator);
			int cpu_count = (int)m_mtjd_manager.getCpuThreadsCount();
			while (m_result.size() < cpu_count)
			{
				m_result.emplace(m_allocator);
			}
		}


		virtual ~CullingSystemImpl()
		{

		}


		virtual void clear() override
		{
			m_spheres.clear();
			m_visibility_flags.clear();
		}


		IAllocator& getAllocator()
		{
			return m_allocator;
		}


		virtual const Results& getResult() override
		{
			if (m_is_async_result)
			{
				m_sync_point.sync();
			}
			return m_result;
		}


		virtual void cullToFrustum(const Frustum& frustum) override
		{
			for (int i = 0; i < m_result.size(); ++i)
			{
				m_result[i].clear();
			}
			doCulling(0, &m_spheres[0], &m_spheres.back(), &m_visibility_flags[0], &frustum, m_result[0]);
			m_is_async_result = false;
		}


		virtual void cullToFrustumAsync(const Frustum& frustum) override
		{
			int count = m_spheres.size();

			if (count == 0)
				return;

			if (count < m_result.size() * MIN_ENTITIES_PER_THREAD)
			{
				cullToFrustum(frustum);
				return;
			}
			m_is_async_result = true;

			int cpu_count = m_mtjd_manager.getCpuThreadsCount();
			int step = count / cpu_count;
			int i = 0;
			CullingJob* jobs[16];
			ASSERT(sizeof(jobs) / sizeof(jobs[0]) >= cpu_count);
			for (; i < cpu_count - 1; i++)
			{
				m_result[i].clear();
				CullingJob* cj = m_allocator.newObject<CullingJob>(m_spheres, m_visibility_flags, m_result[i], i * step, (i + 1) * step - 1, frustum, m_mtjd_manager, m_allocator);
				cj->addDependency(&m_sync_point);
				jobs[i] = cj;
			}

			m_result[i].clear();
			CullingJob* cj = m_allocator.newObject<CullingJob>(m_spheres, m_visibility_flags, m_result[i], i * step, count - 1, frustum, m_mtjd_manager, m_allocator);
			cj->addDependency(&m_sync_point);
			jobs[i] = cj;

			for (i = 0; i < cpu_count; ++i)
			{
				m_mtjd_manager.schedule(jobs[i]);
			}
		}


		virtual void enableStatic(int index) override
		{
			m_visibility_flags[index] = true;
		}


		virtual void disableStatic(int index) override
		{
			m_visibility_flags[index] = false;
		}


		virtual void addStatic(const Sphere& sphere) override
		{
				m_spheres.push(sphere);
				m_visibility_flags.push(true);
		}


		virtual void removeStatic(int index) override
		{
			ASSERT(index <= m_spheres.size());

			m_spheres.erase(index);
			m_visibility_flags.erase(index);
		}


		virtual void updateBoundingRadius(float radius, int index) override
		{
			m_spheres[index].m_radius = radius;
		}


		virtual void updateBoundingPosition(const Vec3& position, int index) override
		{
			m_spheres[index].m_position = position;
		}


		virtual void insert(const InputSpheres& spheres) override
		{
			for (int i = 0; i < spheres.size(); i++)
			{
				m_spheres.push(spheres[i]);
				m_visibility_flags.push(true);
			}
		}


		virtual const InputSpheres& getSpheres() override
		{
			return m_spheres;
		}


	private:
		IAllocator&		m_allocator;
		VisibilityFlags m_visibility_flags;
		InputSpheres	m_spheres;
		Results			m_result;

		MTJD::Manager& m_mtjd_manager;
		MTJD::Group m_sync_point;
		bool m_is_async_result;
	};


	CullingSystem* CullingSystem::create(MTJD::Manager& mtjd_manager, IAllocator& allocator)
	{
		return allocator.newObject<CullingSystemImpl>(mtjd_manager, allocator);
	}


	void CullingSystem::destroy(CullingSystem& culling_system)
	{
		static_cast<CullingSystemImpl&>(culling_system).getAllocator().deleteObject(&culling_system);
	}
}