#include "engine/lumix.hpp"

#include "core/array.hpp"
#include "core/crt.hpp"
#include "core/geometry.hpp"
#include "core/hash_map.hpp"
#include "core/allocator.hpp"
#include "core/atomic.hpp"
#include "core/job_system.hpp"
#include "core/math.hpp"
#include "core/page_allocator.hpp"
#include "core/profiler.hpp"
#include "core/simd.hpp"

#include "culling_system.hpp"


namespace Lumix
{

static_assert(sizeof(CullResult) == PageAllocator::PAGE_SIZE);

struct CellIndices
{
	CellIndices() {}
	CellIndices(const DVec3& pos, float cell_size, u8 type, bool is_big)
		: pos(pos * (1 / cell_size))
		, is_big(is_big)
		, type(type)
	{}

	bool operator==(const CellIndices& rhs) const { return pos == rhs.pos && type == rhs.type && is_big == rhs.is_big; }

	IVec3 pos;
	u8 type;
	bool is_big;
};


struct CellIndicesHasher
{
	// http://www.beosil.com/download/CollisionDetectionHashing_VMV03.pdf
	static u32 get(const CellIndices& indices) {
		// TODO indices.is_big, indices.type
		return (u32)indices.pos.x * 73856093 + (u32)indices.pos.y * 19349663 + (u32)indices.pos.z * 83492791; 
	}
};


struct alignas(4096) CellPage {
	struct {
		CellPage* next = nullptr;
		CellPage* prev = nullptr;
		DVec3 origin;
		CellIndices indices;
		int count = 0;
	} header;

	enum { MAX_COUNT = (PageAllocator::PAGE_SIZE - sizeof(header)) / (sizeof(Sphere) + sizeof(EntityPtr)) };

	Sphere spheres[MAX_COUNT];
	EntityPtr entities[MAX_COUNT];
};

static_assert(sizeof(CellPage) == PageAllocator::PAGE_SIZE);


struct CullingSystemImpl final : CullingSystem
{
	CullingSystemImpl(IAllocator& allocator, PageAllocator& page_allocator) 
		: m_allocator(allocator)
		, m_cell_map(allocator)
		, m_entity_to_cell(allocator)
		, m_cells(allocator)
		, m_cell_size(300.0f)
		, m_page_allocator(page_allocator)
	{
	}
	
	~CullingSystemImpl()
	{
		for(CellPage* page : m_cell_map) {
			ASSERT(!page->header.prev);
			CellPage* iter = page;
			while(iter) {
				CellPage* tmp = iter;
				iter = tmp->header.next;
				tmp->~CellPage();
				m_page_allocator.deallocate(tmp, true);
			}
		}

		m_cells.clear();
		m_cell_map.clear();
		m_entity_to_cell.clear();
	}
	
	Sphere* addToCell(CellPage& cell, EntityPtr entity, const DVec3& pos, float radius)
	{
		const Vec3 rel_pos = Vec3(pos - cell.header.origin);
		const int count = cell.header.count;

		if(count < CellPage::MAX_COUNT - 1) {
			cell.spheres[count] = {rel_pos, radius};
			cell.entities[count] = entity;
			++cell.header.count;
			return &cell.spheres[count];
		}

		void* mem = m_page_allocator.allocate(true);
		CellPage* new_cell = new (Lumix::NewPlaceholder(), mem) CellPage;
		new_cell->header.origin = cell.header.origin;
		new_cell->header.indices = cell.header.indices;
		new_cell->header.next = &cell;
		new_cell->header.prev = cell.header.prev;
		
		new_cell->header.next->header.prev = new_cell;
		if (new_cell->header.prev) new_cell->header.prev->header.next = new_cell;

		m_cells.push(new_cell);
		if(!new_cell->header.prev) m_cell_map[new_cell->header.indices] = new_cell;

		new_cell->spheres[0] = {rel_pos, radius};
		new_cell->entities[0] = entity;
		new_cell->header.count = 1;

		return &new_cell->spheres[0];
	}


	void add(EntityRef entity, u8 type, const DVec3& pos, float radius) override
	{
		if(m_entity_to_cell.size() <= entity.index) {
			m_entity_to_cell.reserve(entity.index);
			while(m_entity_to_cell.size() <= entity.index) {
				m_entity_to_cell.push(nullptr);
			}
		}
		
		const CellIndices i(pos, m_cell_size, type, radius > m_cell_size);

		auto iter = m_cell_map.find(i);
		if (!iter.isValid()) {
			void* mem = m_page_allocator.allocate(true);
			CellPage* new_cell = new (Lumix::NewPlaceholder(), mem) CellPage;
			new_cell->header.origin = i.pos * double(m_cell_size);
			new_cell->header.indices = i;
			m_cell_map.insert(i, new_cell);
			m_cells.push(new_cell);
			iter = m_cell_map.find(i);
		}

		CellPage& cell = *iter.value();
		Sphere* sphere = addToCell(cell, entity, pos, radius);
		m_entity_to_cell[entity.index] = sphere;
		return;
	}


	void remove(EntityRef entity) override
	{
		if (m_entity_to_cell.size() <= entity.index) return;
		
		const Sphere* sphere = m_entity_to_cell[entity.index];
		if (!sphere) return;

		CellPage& cell = getCell(*sphere);
		if (cell.header.count == 1) {
			if (!cell.header.prev) {
				if (!cell.header.next) m_cell_map.erase(cell.header.indices);
				else m_cell_map[cell.header.indices] = cell.header.next;
			}
			if (cell.header.prev) cell.header.prev->header.next = cell.header.next;
			if (cell.header.next) cell.header.next->header.prev = cell.header.prev;
			m_cells.swapAndPopItem(&cell);
			cell.~CellPage();
			m_page_allocator.deallocate(&cell, true);
		}
		else {
			const int idx = int(sphere - cell.spheres);
			EntityPtr last = cell.entities[cell.header.count - 1];
			cell.entities[idx] = cell.entities[cell.header.count - 1];
			cell.spheres[idx] = cell.spheres[cell.header.count - 1];
			m_entity_to_cell[last.index] = &cell.spheres[idx];
			--cell.header.count;
		}
		m_entity_to_cell[entity.index] = nullptr;
	}


	CellPage& getCell(const Sphere& sphere) const
	{
		const intptr_t ptr = (intptr_t)&sphere;
		const intptr_t page_ptr = ptr - (ptr % PageAllocator::PAGE_SIZE);
		return *(CellPage*)page_ptr;
	}


	void setPosition(EntityRef entity, const DVec3& pos) override
	{
		Sphere* sphere = m_entity_to_cell[entity.index];
		CellPage& cell = getCell(*sphere);

		const IVec3 new_indices(pos * (1 / m_cell_size));

		if(new_indices == cell.header.indices.pos) {
			sphere->position = Vec3(pos - cell.header.origin);
			return;
		}

		const float radius = sphere->radius;
		const u8 type = cell.header.indices.type;
		remove(entity);
		add(entity, type, pos, radius);
	}


	float getRadius(EntityRef entity) override
	{
		return m_entity_to_cell[entity.index]->radius;
	}

	void set(EntityRef entity, const DVec3& pos, float radius) override {
		Sphere* sphere = m_entity_to_cell[entity.index];
		CellPage& cell = getCell(*sphere);
		const IVec3 new_indices(pos * (1 / m_cell_size));
		
		const bool was_big = cell.header.indices.is_big;
		const bool is_big = radius > m_cell_size;

		if (was_big == is_big && new_indices == cell.header.indices.pos) {
			sphere->radius = radius;
			sphere->position = Vec3(pos - cell.header.origin);
			return;
		}

		const u8 type = cell.header.indices.type;
		remove(entity);
		add(entity, type, pos, radius);
	}
	
	void setRadius(EntityRef entity, float radius) override
	{
		Sphere* sphere = m_entity_to_cell[entity.index];
		CellPage& cell = getCell(*sphere);
		
		const bool was_big = cell.header.indices.is_big;
		const bool is_big = radius > m_cell_size;

		if (was_big == is_big) {
			sphere->radius = radius;
			return;
		}
		const u8 type = cell.header.indices.type;
		const DVec3 pos = cell.header.origin + sphere->position;
		remove(entity);
		add(entity, type, pos, radius);
	}

	LUMIX_FORCE_INLINE void doCulling(const CellPage& cell
		, const Frustum& frustum
		, CullResult* LUMIX_RESTRICT results
		, PagedList<CullResult>& list
		, u8 type)
	{
		const Sphere* LUMIX_RESTRICT start = cell.spheres;
		const Sphere* LUMIX_RESTRICT end = cell.spheres + cell.header.count;
		const EntityPtr* LUMIX_RESTRICT sphere_to_entity_map = cell.entities;

		const float4 px = f4Load(frustum.xs);
		const float4 py = f4Load(frustum.ys);
		const float4 pz = f4Load(frustum.zs);
		const float4 pd = f4Load(frustum.ds);
		const float4 px2 = f4Load(&frustum.xs[4]);
		const float4 py2 = f4Load(&frustum.ys[4]);
		const float4 pz2 = f4Load(&frustum.zs[4]);
		const float4 pd2 = f4Load(&frustum.ds[4]);
		int cursor = results->header.count;

		int i = 0;

		for (const Sphere *sphere = start; sphere < end; ++sphere, ++i) {
			const float4 cx = f4Splat(sphere->position.x);
			const float4 cy = f4Splat(sphere->position.y);
			const float4 cz = f4Splat(sphere->position.z);
			const float4 r = f4Splat(-sphere->radius);

			float4 t = cx * px + cy * py + cz * pz + pd;
			t = t - r;
			if (f4MoveMask(t)) continue;

			t = cx * px2 + cy * py2 + cz * pz2 + pd2;
			t = t - r;
			if (f4MoveMask(t)) continue;

			if(cursor == lengthOf(results->entities)) {
				results->header.count = cursor;
				results = list.push();
				results->header.type = type;
				cursor = 0;
			}

			results->entities[cursor] = (EntityRef)sphere_to_entity_map[i];
			++cursor;
		}
		results->header.count = cursor;
	}

	CullResult* cull(const ShiftedFrustum& frustum, u8 type) override
	{
		ASSERT(type != 0xff); // 0xff type is reserved for `all types`
		return cullInternal(frustum, type);
	}

	CullResult* cull(const ShiftedFrustum& frustum) override
	{
		return cullInternal(frustum, 0xff);
	}
	
	CullResult* cullInternal(const ShiftedFrustum& frustum, u8 type)
	{
		if (m_cells.empty()) return nullptr;

		AtomicI32 cell_idx = 0;
		PagedList<CullResult> list(m_page_allocator);

		jobs::runOnWorkers([&](){
			PROFILE_BLOCK("culling");
			const Vec3 v3_cell_size(m_cell_size);
			const Vec3 v3_2_cell_size(2 * m_cell_size);
			CullResult* result = nullptr;
			u32 total_count = 0;
			for(;;) {
				const i32 idx = cell_idx.inc();
				if (idx >= m_cells.size()) return;

				CellPage& cell = *m_cells[idx];
				if (type != 0xff && cell.header.indices.type != type) continue;
				if (!result || result->header.type != cell.header.indices.type) {
					result = list.push();
					result->header.type = cell.header.indices.type;
				}

				total_count += cell.header.count;
				if (cell.header.indices.is_big) {
					doCulling(cell, frustum.getRelative(cell.header.origin), result, list, cell.header.indices.type);
				}
				else if (frustum.containsAABB(cell.header.origin + v3_cell_size, v3_cell_size)) {
					int to_cpy = cell.header.count;
					int src_offset = 0;
					while (to_cpy > 0) {
						if(result->header.count == lengthOf(result->entities)) {
							result = list.push();
							result->header.type = cell.header.indices.type;
						}
						const int rem_space = lengthOf(result->entities) - result->header.count;
						const int step = minimum(to_cpy, rem_space);
						memcpy(result->entities + result->header.count, cell.entities + src_offset, step * sizeof(cell.entities[0]));
						src_offset += step;
						result->header.count += step;
						to_cpy -= step;
					}
				}
				else if (frustum.intersectsAABB(cell.header.origin - v3_cell_size, v3_2_cell_size)) {
					doCulling(cell, frustum.getRelative(cell.header.origin), result, list, cell.header.indices.type);
				}
			}
			profiler::pushInt("count", total_count);
		});

		return list.detach();
	}
	

	bool isAdded(EntityRef entity) override
	{
		return entity.index < m_entity_to_cell.size() && m_entity_to_cell[entity.index] != nullptr;
	}


	IAllocator& m_allocator;
	PageAllocator& m_page_allocator;
	HashMap<CellIndices, CellPage*, CellIndicesHasher> m_cell_map;
	Array<CellPage*> m_cells;
	Array<Sphere*> m_entity_to_cell;
	float m_cell_size;
};



void CullResult::free(PageAllocator& allocator)
{
	CullResult* i = this;
	while(i) {
		CullResult* tmp = i;
		i = i->header.next;
		allocator.deallocate(tmp, true);
	}
}


UniquePtr<CullingSystem> CullingSystem::create(IAllocator& allocator, PageAllocator& page_allocator)
{
	return UniquePtr<CullingSystemImpl>::create(allocator, allocator, page_allocator);
}

}