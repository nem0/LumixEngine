#include "culling_system.h"
#include "engine/array.h"
#include "engine/geometry.h"
#include "engine/hash_map.h"
#include "engine/iallocator.h"
#include "engine/job_system.h"
#include "engine/lumix.h"
#include "engine/math_utils.h"
#include "engine/mt/atomic.h"
#include "engine/profiler.h"
#include "engine/simd.h"
#include <cstring>
#include <Windows.h>

namespace Lumix
{
    

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

struct alignas(4096) CellPage {
    struct {
        CellPage* next = nullptr;
        CellPage* prev = nullptr;
	    DVec3 origin;
	    CellIndices indices;
        int count = 0;
    } header;

    enum { FOO = sizeof(header) };
    enum { MAX_COUNT = (16384 - sizeof(header)) / (sizeof(Sphere) + sizeof(EntityPtr)) };

    Sphere spheres[MAX_COUNT];
    EntityPtr entities[MAX_COUNT];
};

static_assert(sizeof(CellPage) == 16384);

struct CellIndicesHasher
{
	// http://www.beosil.com/download/CollisionDetectionHashing_VMV03.pdf
	// TODO check collisions
	static u32 get(const CellIndices& indices) {
        // TODO indices.is_big, indices.type
		return (u32)indices.pos.x * 73856093 + (u32)indices.pos.y * 19349663 + (u32)indices.pos.z * 83492791; 
	}
};


static void doCulling(const Sphere* LUMIX_RESTRICT start,
	const Sphere* LUMIX_RESTRICT end,
	const Frustum* LUMIX_RESTRICT frustum,
	const EntityPtr* LUMIX_RESTRICT sphere_to_entity_map,
	CullingSystem::Subresults& results)
{
	PROFILE_FUNCTION();
	PROFILE_INT("objects", int(end - start));
	const float4 px = f4Load(frustum->xs);
	const float4 py = f4Load(frustum->ys);
	const float4 pz = f4Load(frustum->zs);
	const float4 pd = f4Load(frustum->ds);
	const float4 px2 = f4Load(&frustum->xs[4]);
	const float4 py2 = f4Load(&frustum->ys[4]);
	const float4 pz2 = f4Load(&frustum->zs[4]);
	const float4 pd2 = f4Load(&frustum->ds[4]);
	
	int i = 0;
	for (const Sphere *sphere = start; sphere <= end; sphere++, ++i)
	{
		const float4 cx = f4Splat(sphere->position.x);
		const float4 cy = f4Splat(sphere->position.y);
		const float4 cz = f4Splat(sphere->position.z);
		const float4 r = f4Splat(-sphere->radius);

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

		results.push((EntityRef)sphere_to_entity_map[i]);
	}
}


struct CullingSystemImpl final : public CullingSystem
{
	CullingSystemImpl(IAllocator& allocator) 
		: m_allocator(allocator)
		, m_cell_map(allocator)
		, m_entity_to_cell(allocator)
        , m_cells(allocator)
		, m_cell_size(300.0f)
	{
	}
	
	~CullingSystemImpl()
	{
        clear();
	}
    
	Sphere* addToCell(CellPage& cell, EntityPtr entity, const DVec3& pos, float radius)
	{
		const Vec3 rel_pos = (pos - cell.header.origin).toFloat();
		const int count = cell.header.count;

        if(count < CellPage::MAX_COUNT - 1) {
            cell.spheres[count] = {rel_pos, radius};
            cell.entities[count] = entity;
            ++cell.header.count;
            return &cell.spheres[count];
        }

        void* mem = VirtualAlloc(nullptr, 16384, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
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
            void* mem = VirtualAlloc(nullptr, 16384, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
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
		if(m_entity_to_cell.size() <= entity.index) return;
		
        const Sphere* sphere = m_entity_to_cell[entity.index];
		CellPage& cell = getCell(*sphere);
        if(cell.header.count == 1) {
            if(!cell.header.prev) {
                if(!cell.header.next) m_cell_map.erase(cell.header.indices);
                else m_cell_map[cell.header.indices] = cell.header.next;
            }
            if(cell.header.prev) cell.header.prev->header.next = cell.header.next;
            if(cell.header.next) cell.header.next->header.prev = cell.header.prev;
            m_cells.eraseItemFast(&cell);
            VirtualFree(&cell, 16384, MEM_RELEASE);
        }
        else {
            const int idx = int(sphere - cell.spheres);
            EntityPtr last = cell.entities[cell.header.count - 1];
            cell.entities[idx] = cell.entities[cell.header.count - 1];
            cell.spheres[idx] = cell.spheres[cell.header.count - 1];
            m_entity_to_cell[last.index] = &cell.spheres[idx];
        }
        m_entity_to_cell[entity.index] = nullptr;
	}


    CellPage& getCell(const Sphere& sphere) const
    {
        const intptr_t ptr = (intptr_t)&sphere;
        const intptr_t page_ptr = ptr - (ptr % 16384);
        return *(CellPage*)page_ptr;
    }


	void setPosition(EntityRef entity, const DVec3& pos) override
	{
		Sphere* sphere = m_entity_to_cell[entity.index];
        CellPage& cell = getCell(*sphere);

		const CellIndices old_indices = cell.header.indices;

        const IVec3 new_indices(pos * (1 / m_cell_size));

		if(new_indices == cell.header.indices.pos) {
			sphere->position = (pos - cell.header.origin).toFloat();
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


	void clear() override
	{
        for(CellPage* page : m_cell_map) {
            ASSERT(!page->header.prev);
            CellPage* iter = page;
            while(iter) {
                CellPage* tmp = iter;
                iter = tmp->header.next;
                VirtualFree(tmp, 16384, MEM_RELEASE);
            }
        }
       
        m_cells.clear();
		m_cell_map.clear();
		m_entity_to_cell.clear();
	}


    struct CullJob {
        CullJob(const CullingSystemImpl& system, const ShiftedFrustum& frustum, Subresults& result) 
            : system(system)
            , frustum(frustum)
            , result(result)
        {}


        static void run(void* data) {
            ((CullJob*)data)->run();
        }


        void run() {
            PROFILE_FUNCTION();
		    const Vec3 v3_cell_size(system.m_cell_size);
		    const Vec3 v3_2_cell_size(2 * system.m_cell_size);
            for(;;) {
                const i32 idx = MT::atomicIncrement(cell_idx) - 1;
                if (idx >= system.m_cells.size()) return;

                CellPage& cell = *system.m_cells[idx];
                if(cell.header.indices.type != type) continue;

			    if (frustum.containsAABB(cell.header.origin + v3_cell_size, -v3_cell_size)) {
				    const int count = result.size();
                    result.resize(count + cell.header.count);
                    memcpy(result.begin() + count, cell.entities, sizeof(cell.entities[0]) * cell.header.count);
			    }
			    else if (frustum.intersectsAABB(cell.header.origin - v3_cell_size, v3_2_cell_size)) {
					const Frustum rel_frustum = frustum.getRelative(cell.header.origin);
                    doCulling(cell.spheres, cell.spheres + cell.header.count, &rel_frustum, cell.entities, result);
			    }
            }
        }


        const CullingSystemImpl& system;
        ShiftedFrustum frustum;
        Subresults& result;
        u8 type;
        volatile i32* cell_idx = 0;
    };


    void cull(const ShiftedFrustum& frustum, u8 type, Results& result) override
	{
		PROFILE_FUNCTION();
		if (m_cells.empty()) return;

		const uint buckets_count = MT::getCPUsCount();
		while(result.size() < (int)buckets_count) {
            result.emplace(m_allocator);
            result.back().reserve(100'000);
        }

        JobSystem::SignalHandle signal = JobSystem::INVALID_HANDLE;
        volatile i32 cell_idx = 0;

        CullJob* jobs = (CullJob*)alloca(sizeof(CullJob) * result.size());
        for(int i = 0; i < result.size(); ++i) {
            CullJob* job = new (NewPlaceholder(), jobs + i) CullJob(*this, frustum, result[i]);
            job->cell_idx = &cell_idx;
            job->type = type;
            JobSystem::run(job, CullJob::run, &signal);
        }
        JobSystem::wait(signal);
	}
	

	bool isAdded(EntityRef entity) override
	{
		return entity.index < m_entity_to_cell.size() && m_entity_to_cell[entity.index] != nullptr;
	}


	IAllocator& m_allocator;
	HashMap<CellIndices, CellPage*, CellIndicesHasher> m_cell_map;
    Array<CellPage*> m_cells;
	Array<Sphere*> m_entity_to_cell;
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