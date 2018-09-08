#include "culling_system.h"
#include "engine/array.h"
#include "engine/geometry.h"
#include "engine/job_system.h"
#include "engine/lumix.h"
#include "engine/math_utils.h"
#include "engine/profiler.h"
#include "engine/simd.h"

namespace Lumix
{
typedef Array<u64> LayerMasks;
typedef Array<int> ModelInstancetoSphereMap;
typedef Array<EntityRef> SphereToModelInstanceMap;


static void doCulling(int start_index,
	const Sphere* LUMIX_RESTRICT start,
	const Sphere* LUMIX_RESTRICT end,
	const Frustum* LUMIX_RESTRICT frustum,
	const u64* LUMIX_RESTRICT layer_masks,
	const EntityRef* LUMIX_RESTRICT sphere_to_model_instance_map,
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
	u64 layer_mask;
	int start;
	int end;
	const Frustum* frustum;
};

class CullingSystemImpl final : public CullingSystem
{
public:
	explicit CullingSystemImpl(IAllocator& allocator)
		: m_allocator(allocator)
		, m_spheres(allocator)
		, m_layer_masks(m_allocator)
		, m_sphere_to_model_instance_map(m_allocator)
		, m_model_instance_to_sphere_map(m_allocator)
	{
		m_model_instance_to_sphere_map.reserve(5000);
		m_sphere_to_model_instance_map.reserve(5000);
		m_spheres.reserve(5000);
	}


	void clear() override
	{
		m_spheres.clear();
		m_layer_masks.clear();
		m_model_instance_to_sphere_map.clear();
		m_sphere_to_model_instance_map.clear();
	}


	IAllocator& getAllocator() { return m_allocator; }


	static void cullTask(void* data)
	{
		CullingJobData* cull_data = (CullingJobData*)data;
		if (cull_data->end < cull_data->start) return;
		doCulling(cull_data->start
			, &(*cull_data->spheres)[cull_data->start]
			, &(*cull_data->spheres)[cull_data->end]
			, cull_data->frustum
			, &(*cull_data->layer_masks)[0]
			, &(*cull_data->sphere_to_model_instance_map)[0]
			, cull_data->layer_mask
			, *cull_data->results);
	}


	void cull(const Frustum& frustum, u64 layer_mask, Results& result) override
	{
		const int count = m_spheres.size();
		if(count == 0) return;

		if (result.empty()) {
			const int cpus_count = MT::getCPUsCount();
			const int buckets_count = Math::minimum(cpus_count * 4, count);
			while(result.size() < buckets_count) result.emplace(m_allocator);
		}

		int step = count / result.size();
		
		// TODO allocate on stack
		Array<CullingJobData> job_data(m_allocator);
		Array<JobSystem::JobDecl> jobs(m_allocator);
		job_data.resize(result.size());
		jobs.resize(result.size());
	
		for (int i = 0; i < result.size(); i++) {
			result[i].clear();
			job_data[i] = {
				&m_spheres,
				&result[i],
				&m_layer_masks,
				&m_sphere_to_model_instance_map,
				layer_mask,
				i * step,
				i == result.size() - 1 ? count - 1 : (i + 1) * step - 1,
				&frustum
			};
			jobs[i].data = &job_data[i];
			jobs[i].task = &cullTask;
		}
		volatile int job_counter = 0;
		JobSystem::runJobs(&jobs[0], result.size(), &job_counter);
		JobSystem::wait(&job_counter);
	}


	void setLayerMask(EntityRef model_instance, u64 layer) override
	{
		m_layer_masks[m_model_instance_to_sphere_map[model_instance.index]] = layer;
	}


	u64 getLayerMask(EntityRef model_instance) override
	{
		return m_layer_masks[m_model_instance_to_sphere_map[model_instance.index]];
	}


	bool isAdded(EntityRef model_instance) override
	{
		return model_instance.index < m_model_instance_to_sphere_map.size() && m_model_instance_to_sphere_map[model_instance.index] != -1;
	}


	void addStatic(EntityRef model_instance, const Sphere& sphere, u64 layer_mask) override
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


	void removeStatic(EntityRef model_instance) override
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


	void updateBoundingSphere(const Sphere& sphere, EntityRef model_instance) override
	{
		int idx = m_model_instance_to_sphere_map[model_instance.index];
		if (idx >= 0) m_spheres[idx] = sphere;
	}


	void insert(const InputSpheres& spheres, const Array<EntityRef>& model_instances) override
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


	const Sphere& getSphere(EntityRef model_instance) override
	{
		return m_spheres[m_model_instance_to_sphere_map[model_instance.index]];
	}


private:
	IAllocator& m_allocator;
	InputSpheres m_spheres;
	LayerMasks m_layer_masks;
	ModelInstancetoSphereMap m_model_instance_to_sphere_map;
	SphereToModelInstanceMap m_sphere_to_model_instance_map;
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