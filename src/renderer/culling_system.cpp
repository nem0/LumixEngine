#include "culling_system.h"
#include "engine/array.h"
#include "engine/geometry.h"
#include "engine/hash_map.h"
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

struct CellIndices
{
	CellIndices() {}
	CellIndices(const DVec3& pos, float cell_size)
		: i(int(pos.x / cell_size))
		, j(int(pos.y / cell_size))
		, k(int(pos.z / cell_size))
	{}

	bool operator==(const CellIndices& rhs) const
	{
		return i == rhs.i && j == rhs.j && k == rhs.k;
	}

	int i, j, k;
};

struct CellIndicesHasher
{
	// http://www.beosil.com/download/CollisionDetectionHashing_VMV03.pdf
	// TODO check collisions
	static u32 get(const CellIndices& indices) {
		return (u32)indices.i * 73856093 + (u32)indices.j * 19349663 + (u32)indices.k * 83492791; 
	}
};

struct Cell
{
	Cell(IAllocator& allocator) 
		: spheres(allocator)
		, entities(allocator)
		, masks(allocator)
	{}

	DVec3 origin;
	CellIndices indices;

	Array<Sphere> spheres; 
	Array<EntityRef> entities;
	Array<u64> masks;
};

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


struct CullingSystemImpl final : public CullingSystem
{
	CullingSystemImpl(IAllocator& allocator) 
		: m_allocator(allocator)
		, m_cells(allocator)
		, m_cell_map(allocator)
		, m_entity_to_cell(allocator)
		, m_cell_size(300.0f)
		, m_big_object_cell(allocator)
	{
		m_big_object_cell.origin = {0, 0, 0};
		m_big_object_cell.indices = CellIndices(DVec3(0), m_cell_size);
	}
	
	~CullingSystemImpl()
	{
		for(Cell* cell : m_cells) {
			LUMIX_DELETE(m_allocator, cell);
		}
	}

	u32 addToCell(Cell& cell, EntityRef entity, const DVec3& pos, float radius, u64 mask)
	{
		const Vec3 rel_pos = (pos - cell.origin).toFloat();
		cell.spheres.push({rel_pos, radius});
		cell.masks.push(mask);
		cell.entities.push(entity);
		return cell.entities.size() - 1;
	}

	Cell& getCell(const DVec3& pos)
	{
		const CellIndices i(pos, m_cell_size);

		auto iter = m_cell_map.find(i);
		if (!iter.isValid()) {
			Cell* new_cell = LUMIX_NEW(m_allocator, Cell)(m_allocator);
			new_cell->origin.x = i.i * m_cell_size;
			new_cell->origin.y = i.j * m_cell_size;
			new_cell->origin.z = i.k * m_cell_size;
			new_cell->indices = i;
			m_cells.push(new_cell);
			m_cell_map.insert(i, new_cell);
			iter = m_cell_map.find(i);
		}
		return *iter.value();
	}

	void add(EntityRef entity, const DVec3& pos, float radius, u64 layer_mask) override
	{
		if(m_entity_to_cell.size() <= entity.index) {
			m_entity_to_cell.reserve(entity.index);
			while(m_entity_to_cell.size() <= entity.index) {
				m_entity_to_cell.push({nullptr, 0});
			}
		}
		
		if (radius > m_cell_size) {
			const u32 idx = addToCell(m_big_object_cell, entity, pos, radius, layer_mask);
			m_entity_to_cell[entity.index] = {&m_big_object_cell, idx};
			return;
		}

		Cell& cell = getCell(pos);
		const u32 idx = addToCell(cell, entity, pos, radius, layer_mask);
		m_entity_to_cell[entity.index] = {&cell, idx}; 
		return;
	}

	void remove(EntityRef entity) override
	{
		const EntityLoc loc = m_entity_to_cell[entity.index];
		Cell& cell = *loc.cell;
		const EntityRef last = cell.entities.back();
		m_entity_to_cell[last.index] = loc;
		cell.entities.eraseFast(loc.idx);
		cell.masks.eraseFast(loc.idx);
		cell.spheres.eraseFast(loc.idx);

		if(cell.spheres.empty() && &cell != &m_big_object_cell) {
			m_cells.eraseItem(&cell);
			m_cell_map.erase(cell.indices);
			LUMIX_DELETE(m_allocator, &cell);
		}
	}

	void setPosition(EntityRef entity, const DVec3& pos) override
	{
		const EntityLoc loc = m_entity_to_cell[entity.index];
		Cell& cell = *loc.cell;
		if (&cell == &m_big_object_cell) {
			cell.spheres[loc.idx].position = (pos - cell.origin).toFloat();
			return;
		}

		const CellIndices new_indices(pos, m_cell_size);
		const CellIndices old_indices = cell.indices;

		if(old_indices == new_indices) {
			cell.spheres[loc.idx].position = (pos - cell.origin).toFloat();
			return;
		}

		const float radius = cell.spheres[loc.idx].radius;
		const u64 layer_mask = cell.masks[loc.idx];
		remove(entity);
		add(entity, pos, radius, layer_mask);
	}

	float getRadius(EntityRef entity) override
	{
		const EntityLoc loc = m_entity_to_cell[entity.index];
		const Cell& cell = *loc.cell;
		return cell.spheres[loc.idx].radius;
	}

	void setRadius(EntityRef entity, float radius) override
	{
		const EntityLoc loc = m_entity_to_cell[entity.index];
		Cell& cell = *loc.cell;
		
		const bool was_big = &cell == &m_big_object_cell;
		const bool is_big = radius > m_cell_size;

		if (was_big == is_big) {
			cell.spheres[loc.idx].radius = radius;
			return;
		}

		const DVec3 pos = cell.origin + cell.spheres[loc.idx].position;
		const u64 layer_mask = cell.masks[loc.idx];

		remove(entity);
		add(entity, pos, radius, layer_mask);
	}
	
	void setLayerMask(EntityRef entity, u64 layer) override
	{
		const EntityLoc loc = m_entity_to_cell[entity.index];
		Cell& cell = *loc.cell;
		cell.masks[loc.idx] = layer;
	}

	void clear() override
	{
		for(Cell* cell : m_cells) {
			LUMIX_DELETE(m_allocator, cell);
		}
		m_cells.clear();
		m_cell_map.clear();
		m_entity_to_cell.clear();
		
		m_big_object_cell.spheres.clear();
		m_big_object_cell.masks.clear();
		m_big_object_cell.entities.clear();
	}

	void cull(const Frustum& frustum, u64 layer_mask, Results& result) override
	{
		ASSERT(result.empty());
		PROFILE_FUNCTION();
		if(m_cells.empty() && m_big_object_cell.entities.empty()) return;

		const int buckets_count = MT::getCPUsCount() * 4;
		while(result.size() < buckets_count) result.emplace(m_allocator);

		for (Cell* cell : m_cells) {
/*			const AABB aabb(Vec3(0), Vec3(m_cell_size));
			if (frustum.intersectAABB(aabb)) {
				
			}*/

			for(EntityRef e : cell->entities) {
				result[0].push(e);
			}
		}
		for(EntityRef e : m_big_object_cell.entities) {
			result[0].push(e);
		}
/*
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
		JobSystem::wait(&job_counter);*/
	}
	

	bool isAdded(EntityRef entity) override
	{
		return m_entity_to_cell[entity.index].cell != nullptr;
	}


	struct EntityLoc
	{
		Cell* cell;
		u32 idx;
	};

	IAllocator& m_allocator;
	Array<Cell*> m_cells;
	HashMap<CellIndices, Cell*, CellIndicesHasher> m_cell_map;
	Cell m_big_object_cell;
	Array<EntityLoc> m_entity_to_cell;
	float m_cell_size;
};

/*
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
*/

CullingSystem* CullingSystem::create(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, CullingSystemImpl)(allocator);
}


void CullingSystem::destroy(CullingSystem& culling_system)
{
	LUMIX_DELETE(static_cast<CullingSystemImpl&>(culling_system).m_allocator, &culling_system);
}
}