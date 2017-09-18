#include "culling_system.h"
#include "engine/array.h"
#include "engine/binary_array.h"
#include "engine/free_list.h"
#include "engine/geometry.h"
#include "engine/job_system.h"
#include "engine/lumix.h"
#include "engine/profiler.h"
#include "engine/simd.h"

namespace Lumix
{
typedef Array<u64> LayerMasks;
typedef Array<int> ModelInstancetoSphereMap;
typedef Array<ComponentHandle> SphereToModelInstanceMap;

static const int MIN_ENTITIES_PER_THREAD = 50;

static void doCulling(int start_index,
	const Sphere* LUMIX_RESTRICT start,
	const Sphere* LUMIX_RESTRICT end,
	const Frustum* LUMIX_RESTRICT frustum,
	const u64* LUMIX_RESTRICT layer_masks,
	const ComponentHandle* LUMIX_RESTRICT sphere_to_model_instance_map,
	u64 layer_mask,
	CullingSystem::Subresults& results)
{
	PROFILE_FUNCTION();
	int i = start_index;
	ASSERT(results.empty());
	PROFILE_INT("objects", int(end - start));
	float4 px = f4Load(frustum->xs);
	float4 py = f4Load(frustum->ys);
	float4 pz = f4Load(frustum->zs);
	float4 pd = f4Load(frustum->ds);
	float4 px2 = f4Load(&frustum->xs[4]);
	float4 py2 = f4Load(&frustum->ys[4]);
	float4 pz2 = f4Load(&frustum->zs[4]);
	float4 pd2 = f4Load(&frustum->ds[4]);
	
	for (const Sphere *sphere = start; sphere <= end; sphere++, ++i)
	{
		float4 cx = f4Splat(sphere->position.x);
		float4 cy = f4Splat(sphere->position.y);
		float4 cz = f4Splat(sphere->position.z);
		float4 r = f4Splat(-sphere->radius);

		float4 t = f4Mul(cx, px);
		t = f4Add(t, f4Mul(cy, py));
		t = f4Add(t, f4Mul(cz, pz));
		t = f4Add(t, pd);
		t = f4Sub(t, r);
		if (f4MoveMask(t)) continue;

		t = f4Mul(cx, px2);
		t = f4Add(t, f4Mul(cy, py2));
		t = f4Add(t, f4Mul(cz, pz2));
		t = f4Add(t, pd2);
		t = f4Sub(t, r);
		if (f4MoveMask(t)) continue;

		if(layer_masks[i] & layer_mask) results.push(sphere_to_model_instance_map[i]);
	}
}

struct CullingJobData
{
	const CullingSystem::InputSpheres* spheres;
	CullingSystem::Subresults* results;
	const LayerMasks* layer_masks;
	const SphereToModelInstanceMap* sphere_to_model_instance_map;
	u64 m_layer_mask;
	int m_start;
	int m_end;
	const Frustum* frustum;
};

class CullingSystemImpl LUMIX_FINAL : public CullingSystem
{
public:
	CullingSystemImpl(IAllocator& allocator)
		: m_allocator(allocator)
		, m_job_allocator(allocator)
		, m_spheres(allocator)
		, m_result(allocator)
		, m_layer_masks(m_allocator)
		, m_sphere_to_model_instance_map(m_allocator)
		, m_model_instance_to_sphere_map(m_allocator)
	{
		m_result.emplace(m_allocator);
		m_model_instance_to_sphere_map.reserve(5000);
		m_sphere_to_model_instance_map.reserve(5000);
		m_spheres.reserve(5000);
		int cpu_count = (int)MT::getCPUsCount();
		while (m_result.size() < cpu_count)
		{
			m_result.emplace(m_allocator);
		}
	}


	void clear() override
	{
		m_spheres.clear();
		m_layer_masks.clear();
		m_model_instance_to_sphere_map.clear();
		m_sphere_to_model_instance_map.clear();
	}


	IAllocator& getAllocator() { return m_allocator; }


	const Results& getResult() override
	{
		return m_result;
	}


	void cullToFrustum(const Frustum& frustum, u64 layer_mask) override
	{
		for (int i = 0; i < m_result.size(); ++i)
		{
			m_result[i].clear();
		}
		if (!m_spheres.empty())
		{
			doCulling(0,
				&m_spheres[0],
				&m_spheres.back(),
				&frustum,
				&m_layer_masks[0],
				&m_sphere_to_model_instance_map[0],
				layer_mask,
				m_result[0]);
		}
		m_is_async_result = false;
	}


	static void cullTask(void* data)
	{

	}


	void cullToFrustumAsync(const Frustum& frustum, u64 layer_mask) override
	{
		int count = m_spheres.size();
		for(auto& i : m_result) i.clear();

		if (count < m_result.size() * MIN_ENTITIES_PER_THREAD)
		{
			cullToFrustum(frustum, layer_mask);
			return;
		}
		m_is_async_result = true;

		int step = count / m_result.size();
		CullingJobData job_data[16];
		JobSystem::JobDecl jobs[16];
		ASSERT(lengthOf(jobs) >= m_result.size());
		for (int i = 0; i < m_result.size(); i++)
		{
			m_result[i].clear();
			job_data[i] = {
				&m_spheres,
				&m_result[i],
				&m_layer_masks,
				&m_sphere_to_model_instance_map,
				layer_mask,
				i * step,
				i == m_result.size() - 1 ? count - 1 : (i + 1) * step - 1,
				&frustum
			};
			jobs[i].data = &job_data[i];
			jobs[i].task = &cullTask;
		}
		volatile int counter = 0;
		JobSystem::runJobs(jobs, m_result.size(), &counter);
		JobSystem::waitOutsideJob(&counter);
	}


	void setLayerMask(ComponentHandle model_instance, u64 layer) override
	{
		m_layer_masks[m_model_instance_to_sphere_map[model_instance.index]] = layer;
	}


	u64 getLayerMask(ComponentHandle model_instance) override
	{
		return m_layer_masks[m_model_instance_to_sphere_map[model_instance.index]];
	}


	bool isAdded(ComponentHandle model_instance) override
	{
		return model_instance.index < m_model_instance_to_sphere_map.size() && m_model_instance_to_sphere_map[model_instance.index] != -1;
	}


	void addStatic(ComponentHandle model_instance, const Sphere& sphere, u64 layer_mask) override
	{
		if (model_instance.index < m_model_instance_to_sphere_map.size() &&
			m_model_instance_to_sphere_map[model_instance.index] != -1)
		{
			ASSERT(false);
			return;
		}

		m_spheres.push(sphere);
		m_sphere_to_model_instance_map.push(model_instance);
		while(model_instance.index >= m_model_instance_to_sphere_map.size())
		{
			m_model_instance_to_sphere_map.push(-1);
		}
		m_model_instance_to_sphere_map[model_instance.index] = m_spheres.size() - 1;
		m_layer_masks.push(layer_mask);
	}


	void removeStatic(ComponentHandle model_instance) override
	{
		if (model_instance.index >= m_model_instance_to_sphere_map.size()) return;
		int index = m_model_instance_to_sphere_map[model_instance.index];
		if (index < 0) return;
		ASSERT(index < m_spheres.size());

		m_model_instance_to_sphere_map[m_sphere_to_model_instance_map.back().index] = index;
		m_spheres[index] = m_spheres.back();
		m_sphere_to_model_instance_map[index] = m_sphere_to_model_instance_map.back();
		m_layer_masks[index] = m_layer_masks.back();

		m_spheres.pop();
		m_sphere_to_model_instance_map.pop();
		m_layer_masks.pop();
		m_model_instance_to_sphere_map[model_instance.index] = -1;
	}


	void updateBoundingSphere(const Sphere& sphere, ComponentHandle model_instance) override
	{
		int idx = m_model_instance_to_sphere_map[model_instance.index];
		if (idx >= 0) m_spheres[idx] = sphere;
	}


	void insert(const InputSpheres& spheres, const Array<ComponentHandle>& model_instances) override
	{
		for (int i = 0; i < spheres.size(); i++)
		{
			m_spheres.push(spheres[i]);
			while(m_model_instance_to_sphere_map.size() <= model_instances[i].index)
			{
				m_model_instance_to_sphere_map.push(-1);
			}
			m_model_instance_to_sphere_map[model_instances[i].index] = m_spheres.size() - 1;
			m_sphere_to_model_instance_map.push(model_instances[i]);
			m_layer_masks.push(1);
		}
	}


	const Sphere& getSphere(ComponentHandle model_instance) override
	{
		return m_spheres[m_model_instance_to_sphere_map[model_instance.index]];
	}


private:
	IAllocator& m_allocator;
	FreeList<CullingJobData, 16> m_job_allocator;
	InputSpheres m_spheres;
	Results m_result;
	LayerMasks m_layer_masks;
	ModelInstancetoSphereMap m_model_instance_to_sphere_map;
	SphereToModelInstanceMap m_sphere_to_model_instance_map;

	bool m_is_async_result;
};


CullingSystem* CullingSystem::create(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, CullingSystemImpl)(allocator);
}


void CullingSystem::destroy(CullingSystem& culling_system)
{
	LUMIX_DELETE(static_cast<CullingSystemImpl&>(culling_system).getAllocator(), &culling_system);
}
}