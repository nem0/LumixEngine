#include "culling_system.h"
#include "engine/array.h"
#include "engine/geometry.h"
#include "engine/hash_map.h"
#include "engine/job_system.h"
#include "engine/lumix.h"
#include "engine/math_utils.h"
#include "engine/profiler.h"
#include "engine/simd.h"
#include <cstring>

namespace Lumix
{

enum {
	ID_ENTITY_BITMASK = 0x00ffFFff,
	ID_TYPE_BITMASK = 0xff000000
};

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
		, ids(allocator)
	{}

	DVec3 origin;
	CellIndices indices;

	Array<Sphere> spheres; 
	Array<u32> ids;
};

static void doCulling(const Sphere* LUMIX_RESTRICT start,
	const Sphere* LUMIX_RESTRICT end,
	const Frustum* LUMIX_RESTRICT frustum,
	const u32* LUMIX_RESTRICT sphere_to_id_map,
	CullingSystem::Subresults& results)
{
	PROFILE_FUNCTION();
	PROFILE_INT("objects", int(end - start));
	float4 px = f4Load(frustum->xs);
	float4 py = f4Load(frustum->ys);
	float4 pz = f4Load(frustum->zs);
	float4 pd = f4Load(frustum->ds);
	float4 px2 = f4Load(&frustum->xs[4]);
	float4 py2 = f4Load(&frustum->ys[4]);
	float4 pz2 = f4Load(&frustum->zs[4]);
	float4 pd2 = f4Load(&frustum->ds[4]);
	
	int i = 0;
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

		results.push(sphere_to_id_map[i]);
	}
}


struct CullingJobData
{
	const CullingSystem::InputSpheres* spheres;
	CullingSystem::Subresults* results;
	const Array<u32>* sphere_to_renderable_map;
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

	u32 addToCell(Cell& cell, u32 id, const DVec3& pos, float radius)
	{
		const Vec3 rel_pos = (pos - cell.origin).toFloat();
		cell.spheres.push({rel_pos, radius});
		cell.ids.push(id);
		return cell.ids.size() - 1;
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

	void add(EntityRef entity, u8 type, const DVec3& pos, float radius) override
	{
		if(m_entity_to_cell.size() <= entity.index) {
			m_entity_to_cell.reserve(entity.index);
			while(m_entity_to_cell.size() <= entity.index) {
				m_entity_to_cell.push({nullptr, 0});
			}
		}
		
		ASSERT((entity.index & ID_TYPE_BITMASK) == 0);
		const u32 id = type << 24 | (entity.index & 0xffFFff);
		if (radius > m_cell_size) {
			const u32 idx = addToCell(m_big_object_cell, id, pos, radius);
			m_entity_to_cell[entity.index] = {&m_big_object_cell, idx};
			return;
		}

		Cell& cell = getCell(pos);
		const u32 idx = addToCell(cell, id, pos, radius);
		m_entity_to_cell[entity.index] = {&cell, idx}; 
		return;
	}

	void remove(EntityRef entity) override
	{
		if(m_entity_to_cell.size() <= entity.index) return;
		const EntityLoc loc = m_entity_to_cell[entity.index];
		Cell& cell = *loc.cell;
		const u32 last = cell.ids.back();
		m_entity_to_cell[last & ID_ENTITY_BITMASK] = loc;
		m_entity_to_cell[entity.index].cell = nullptr;
		cell.ids.eraseFast(loc.idx);
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
		const u32 id = cell.ids[loc.idx];
		remove(entity);
		add(entity, (id & ID_TYPE_BITMASK) >> 24, pos, radius);
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
		const u32 id = cell.ids[loc.idx];

		remove(entity);
		add(entity, (id & ID_TYPE_BITMASK) >> 24, pos, radius);
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
		m_big_object_cell.ids.clear();
	}

	static void pushWhole(Cell* cell, uint buckets_count, int counter, Results& result)
	{
		PROFILE_FUNCTION();
		if(cell->ids.size() < int(buckets_count * 128)) {
			Subresults& subresult = result[counter];
			const int old_size = result[counter].size();
			subresult.resize(result[counter].size() + cell->ids.size());
			memcpy(subresult.begin() + old_size, cell->ids.begin(), cell->ids.byte_size());
			return;
		}
		JobSystem::SignalHandle job_counter = JobSystem::INVALID_HANDLE;
		struct Job {
			void* src;
			void* dst;
			int size;
		};
		Job* jobs = (Job*)alloca(buckets_count * sizeof(Job));
		const int per_bucket = (cell->ids.size() + buckets_count - 1) / buckets_count;
		for (uint b = 0; b < buckets_count; ++b) {
			const int size = Math::minimum(per_bucket, cell->ids.size() - b * per_bucket);;
			const int old_size = result[b].size();
			result[b].resize(result[b].size() + size);
			jobs[b].dst = &result[b][old_size]; 
			jobs[b].src = &cell->ids[b * per_bucket];
			jobs[b].size = size * sizeof(cell->ids[0]);
			JobSystem::run(&jobs[b], [](void* data){
				PROFILE_BLOCK("copy");
				Job* j = (Job*)data;
				memcpy(j->dst, j->src, j->size);
			}, &job_counter);
		}
		JobSystem::wait(job_counter);
	}

	void cull(const ShiftedFrustum& frustum, Results& result) override
	{
		PROFILE_FUNCTION();
		ASSERT(result.empty());
		if(m_cells.empty() && m_big_object_cell.ids.empty()) return;

		const uint buckets_count = MT::getCPUsCount() * 4;
		while(result.size() < (int)buckets_count) result.emplace(m_allocator);

		Array<Cell*> partial(m_allocator);
		const Vec3 v3_cell_size(m_cell_size);
		const Vec3 v3_2_cell_size(2 * m_cell_size);
		uint counter = 0;
		uint partial_entities = 0;
		for (Cell* cell : m_cells) {
			if (frustum.containsAABB(cell->origin + v3_cell_size, -v3_cell_size)) {
				pushWhole(cell, buckets_count, counter, result);
				counter = (counter + 1) % buckets_count;
			}
			else if (frustum.intersectsAABB(cell->origin - v3_cell_size, v3_2_cell_size)) {
				partial_entities += cell->ids.size();
				partial.push(cell);
			}
		}
		
		if(!m_big_object_cell.ids.empty()) {
			partial_entities += m_big_object_cell.ids.size();
			partial.push(&m_big_object_cell);
		}

		struct Job {
			static void execute(void* data)
			{
				PROFILE_BLOCK("cull job");
				Job* that = (Job*)data;

				{
					Cell* cell = *that->cells;
					const Frustum rel_frustum = that->frustum.getRelative(cell->origin);
					doCulling(cell->spheres.begin() + that->entity_start_offset
						, (that->cells == that->cells_end ? cell->spheres.begin() + that->entity_end_offset : cell->spheres.end())
						, &rel_frustum
						, cell->ids.begin() + that->entity_start_offset
						, *that->result);
				}

				for(Cell** i = that->cells + 1; i <= that->cells_end - 1; ++i) {
					Cell* cell = *i;
					const Frustum rel_frustum = that->frustum.getRelative(cell->origin);
					doCulling(cell->spheres.begin()
						, cell->spheres.end()
						, &rel_frustum
						, cell->ids.begin()
						, *that->result);
				}

				if (that->cells != that->cells_end) {
					Cell* cell = *that->cells_end - 1;
					const Frustum rel_frustum = that->frustum.getRelative(cell->origin);
					doCulling(cell->spheres.begin()
						, cell->spheres.begin() + that->entity_end_offset
						, &rel_frustum
						, cell->ids.begin()
						, *that->result);
				}
			}


			Cell** cells;
			Cell** cells_end;
			uint entity_start_offset;
			uint entity_end_offset;
			ShiftedFrustum frustum;
			Subresults* result;
		};
		
		Array<Job> jobs(m_allocator);
		jobs.reserve(Math::minimum(buckets_count, partial_entities));
		PROFILE_INT("jobs", jobs.capacity());
		if (jobs.capacity() > 0) {
			JobSystem::SignalHandle counter = JobSystem::INVALID_HANDLE;
			const uint step = (partial_entities + jobs.capacity() - 1) / jobs.capacity();
			Cell** cell_iter = partial.begin();
			uint entity_offset = 0;
			for(int i = 0; i < jobs.capacity(); ++i) {
				Job& job = jobs.emplace();
				
				job.result = &result[i];
				uint job_entities = step;
				job.cells = cell_iter;
				job.entity_start_offset = entity_offset;
				while(job_entities > (uint)(*cell_iter)->ids.size() && cell_iter + 1 != partial.end()) {
					job_entities -= (uint)(*cell_iter)->ids.size();
					++cell_iter;
				}
				job.cells_end = cell_iter;
				job.entity_end_offset = job_entities - 1;
				entity_offset = job_entities;
				if( job.cells_end == job.cells) {
					job.entity_end_offset += job.entity_start_offset;
					entity_offset = job.entity_end_offset + 1;
				}
				job.entity_end_offset = Math::minimum(job.entity_end_offset, (*job.cells_end)->ids.size() - 1);
				job.frustum = frustum;
				JobSystem::run(&job, &Job::execute, &counter);
			}
			JobSystem::wait(counter);
		}
	}
	

	bool isAdded(EntityRef entity) override
	{
		return entity.index < m_entity_to_cell.size() && m_entity_to_cell[entity.index].cell != nullptr;
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



CullingSystem* CullingSystem::create(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, CullingSystemImpl)(allocator);
}


void CullingSystem::destroy(CullingSystem& culling_system)
{
	LUMIX_DELETE(static_cast<CullingSystemImpl&>(culling_system).m_allocator, &culling_system);
}
}