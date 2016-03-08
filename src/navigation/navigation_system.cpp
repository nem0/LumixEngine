#include "lumix.h"
#include "core/array.h"
#include "core/base_proxy_allocator.h"
#include "core/crc32.h"
#include "core/iallocator.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/vec.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "universe/universe.h"
#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourNavMeshBuilder.h>


namespace Lumix
{


struct NavigationSystem : public IPlugin
{
	NavigationSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
	{}

	bool create() override { return true; }
	void destroy() override {}
	const char* getName() const override { return "navigation"; }


	IScene* createScene(Universe& universe) override;
	void destroyScene(IScene* scene) override;


	BaseProxyAllocator m_allocator;
	Engine& m_engine;
};


struct NavigationScene : public IScene
{
	NavigationScene(NavigationSystem& system, Universe& universe, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(universe)
		, m_system(system)
	{}


	void registerLuaAPI()
	{
		lua_State* L = m_system.m_engine.getState();
		#define REGISTER_FUNCTION(name) \
			do {\
				auto f = &LuaWrapper::wrapMethod<NavigationScene, decltype(&NavigationScene::name), &NavigationScene::name>; \
				LuaWrapper::createSystemFunction(L, "Navigation", #name, f); \
			} while(false) \

		REGISTER_FUNCTION(generateNavmesh);

		#undef REGISTER_FUNCTION
	}


	void sendMessage(uint32 type, void*) override
	{
		static const uint32 register_hash = crc32("registerLuaAPI");
		if (type == register_hash) registerLuaAPI();
	}


	void rasterizeGeometry(rcContext& ctx, rcConfig& cfg, Array<uint8>& triareas, rcHeightfield* solid)
	{
		int ntris = 0;
		const int* tris = nullptr;
		int nverts = 0;
		const float* verts = nullptr;

		triareas.resize(ntris);
		setMemory(&triareas[0], 0, triareas.size());
		rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, nverts, tris, ntris, &triareas[0]);
		rcRasterizeTriangles(&ctx, verts, nverts, tris, &triareas[0], ntris, *solid, cfg.walkableClimb);
	}


	bool generateNavmesh()
	{
		const float voxel_height = 0.1f;
		const float voxel_size = 0.3f;
		const float agent_height = 2.0f;
		const float agent_radius = 0.6f;
		const float agent_max_climb = 0.9f;
		const float detail_sample_dist = 6;
		const float max_edge_length = 12;
		const int min_region_area = 64;
		const int merge_region_area = 400;
		const int max_verts_per_poly = 6;
		const float detail_sample_max_error = 1;

		rcConfig cfg = {};
		cfg.cs = voxel_size;
		cfg.ch = voxel_height;	
		cfg.walkableSlopeAngle = 45.0f;
		cfg.walkableHeight = (int)(agent_height / cfg.ch + 0.99f);
		cfg.walkableClimb = (int)(agent_max_climb / cfg.ch);
		cfg.walkableRadius = (int)(agent_radius / cfg.cs + 0.99f);
		cfg.maxEdgeLen = (int)(max_edge_length / voxel_size);
		cfg.maxSimplificationError = 1.3f;
		cfg.minRegionArea = min_region_area;
		cfg.mergeRegionArea = merge_region_area;
		cfg.maxVertsPerPoly = max_verts_per_poly;
		cfg.detailSampleDist = detail_sample_dist < 0.9f ? 0 : voxel_size * detail_sample_dist;
		cfg.detailSampleMaxError = voxel_height * detail_sample_max_error;

		rcContext ctx;
		Vec3 bmin;
		Vec3 bmax;
		rcVcopy(cfg.bmin, &bmin.x);
		rcVcopy(cfg.bmax, &bmax.x);
		rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
		rcHeightfield* solid = rcAllocHeightfield();
		if (!solid)
		{
			g_log_error.log("Navigation") << "generateNavmesh: Out of memory 'solid'.";
			return false;
		}
		if (!rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
		{
			g_log_error.log("Navigation") << "generateNavmesh: Could not create solid heightfield.";
			return false;
		}
		Array<uint8> triareas(m_allocator);
		rasterizeGeometry(ctx, cfg, triareas, solid);

		rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
		rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
		rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

		rcCompactHeightfield* chf = rcAllocCompactHeightfield();
		if (!chf)
		{
			g_log_error.log("Navigation") << "generateNavmesh: Out of memory 'chf'.";
			return false;
		}

		if (!rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf))
		{
			g_log_error.log("Navigation") << "generateNavmesh: Could not build compact data.";
			return false;
		}

		rcFreeHeightField(solid);
		solid = 0;

		if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
		{
			g_log_error.log("Navigation") << "generateNavmesh: Could not erode.";
			return false;
		}

		if (!rcBuildDistanceField(&ctx, *chf))
		{
			g_log_error.log("Navigation") << "generateNavmesh: Could not build distance field.";
			return false;
		}

		if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
		{
			g_log_error.log("Navigation") << "generateNavmesh: Could not build regions.";
			return false;
		}

		rcContourSet* cset = rcAllocContourSet();
		if (!cset)
		{
			ctx.log(RC_LOG_ERROR, "generateNavmesh: Out of memory 'cset'.");
			return false;
		}
		if (!rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
		{
			g_log_error.log("Navigation") << "generateNavmesh: Could not create contours.";
			return false;
		}

		m_polymesh = rcAllocPolyMesh();
		if (!m_polymesh)
		{
			g_log_error.log("Navigation") << "generateNavmesh: Out of memory 'm_polymesh'.";
			return false;
		}
		if (!rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *m_polymesh))
		{
			g_log_error.log("Navigation") << "generateNavmesh: Could not triangulate contours.";
			return false;
		}

		m_detail_mesh = rcAllocPolyMeshDetail();
		if (!m_detail_mesh)
		{
			g_log_error.log("Navigation") << "generateNavmesh: Out of memory 'pmdtl'.";
			return false;
		}

		if (!rcBuildPolyMeshDetail(&ctx, *m_polymesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *m_detail_mesh))
		{
			g_log_error.log("Navigation") << "generateNavmesh: Could not build detail mesh.";
			return false;
		}

		rcFreeCompactHeightfield(chf);
		chf = 0;
		rcFreeContourSet(cset);
		cset = 0;

		unsigned char* navData = 0;
		int navDataSize = 0;

		for (int i = 0; i < m_polymesh->npolys; ++i)
		{
			m_polymesh->flags[i] = m_polymesh->areas[i] == RC_WALKABLE_AREA ? 1 : 0;
		}

		dtNavMeshCreateParams params = {};
		params.verts = m_polymesh->verts;
		params.vertCount = m_polymesh->nverts;
		params.polys = m_polymesh->polys;
		params.polyAreas = m_polymesh->areas;
		params.polyFlags = m_polymesh->flags;
		params.polyCount = m_polymesh->npolys;
		params.nvp = m_polymesh->nvp;
		params.detailMeshes = m_detail_mesh->meshes;
		params.detailVerts = m_detail_mesh->verts;
		params.detailVertsCount = m_detail_mesh->nverts;
		params.detailTris = m_detail_mesh->tris;
		params.detailTriCount = m_detail_mesh->ntris;
		params.walkableHeight = (float)cfg.walkableHeight;
		params.walkableRadius = (float)cfg.walkableRadius;
		params.walkableClimb = (float)cfg.walkableClimb;
		rcVcopy(params.bmin, m_polymesh->bmin);
		rcVcopy(params.bmax, m_polymesh->bmax);
		params.cs = cfg.cs;
		params.ch = cfg.ch;
		params.buildBvTree = true;

		if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
		{
			g_log_error.log("Navigation") << "Could not build Detour navmesh.";
			return false;
		}

		m_navmesh = dtAllocNavMesh();
		if (!m_navmesh)
		{
			dtFree(navData);
			g_log_error.log("Navigation") << "Could not create Detour navmesh";
			return false;
		}

		dtStatus status;

		status = m_navmesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
		if (dtStatusFailed(status))
		{
			dtFree(navData);
			g_log_error.log("Navigation") << "Could not init Detour navmesh";
			return false;
		}

		m_navquery = dtAllocNavMeshQuery();
		status = m_navquery->init(m_navmesh, 2048);
		if (dtStatusFailed(status))
		{
			g_log_error.log("Navigation") << "Could not init Detour navmesh query";
			return false;
		}

		return true;
	}


	ComponentIndex createComponent(uint32, Entity) override { return INVALID_COMPONENT; }
	void destroyComponent(ComponentIndex component, uint32 type) override {}
	void serialize(OutputBlob& serializer) override {}
	void deserialize(InputBlob& serializer, int version) override {}
	IPlugin& getPlugin() const override { return m_system; }
	void update(float time_delta, bool paused) override {}
	bool ownComponentType(uint32 type) const override { return false; }
	ComponentIndex getComponent(Entity entity, uint32 type) override { return INVALID_COMPONENT; }
	Universe& getUniverse() override { return m_universe; }


	IAllocator& m_allocator;
	Universe& m_universe;
	NavigationSystem& m_system;
	rcPolyMesh* m_polymesh;
	dtNavMesh* m_navmesh;
	dtNavMeshQuery* m_navquery;
	rcPolyMeshDetail* m_detail_mesh;
};


IScene* NavigationSystem::createScene(Universe& universe)
{
	return LUMIX_NEW(m_allocator, NavigationScene)(*this, universe, m_allocator);
}


void NavigationSystem::destroyScene(IScene* scene)
{
	LUMIX_DELETE(m_allocator, scene);
}



LUMIX_PLUGIN_ENTRY(navigation)
{
	return LUMIX_NEW(engine.getAllocator(), NavigationSystem)(engine);
}


} // namespace Lumix
