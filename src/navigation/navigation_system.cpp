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
#include "renderer/model.h"
#include "renderer/material.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include "universe/universe.h"
#include <cmath>
#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>
#include <RecastAlloc.h>


namespace Lumix
{


static const float SIZE = 64;


struct NavigationSystem : public IPlugin
{
	NavigationSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
	{
		ASSERT(s_instance == nullptr);
		s_instance = this;
		dtAllocSetCustom(&detourAlloc, &detourFree);
		rcAllocSetCustom(&recastAlloc, &recastFree);
	}


	~NavigationSystem()
	{
		s_instance = nullptr;
	}


	static void detourFree(void* ptr)
	{
		s_instance->m_allocator.deallocate(ptr);
	}


	static void* detourAlloc(size_t size, dtAllocHint hint)
	{
		return s_instance->m_allocator.allocate(size);
	}


	static void recastFree(void* ptr)
	{
		s_instance->m_allocator.deallocate(ptr);
	}


	static void* recastAlloc(size_t size, rcAllocHint hint)
	{
		return s_instance->m_allocator.allocate(size);
	}


	static NavigationSystem* s_instance;


	bool create() override { return true; }
	void destroy() override {}
	const char* getName() const override { return "navigation"; }
	IScene* createScene(Universe& universe) override;
	void destroyScene(IScene* scene) override;

	BaseProxyAllocator m_allocator;
	Engine& m_engine;
};


NavigationSystem* NavigationSystem::s_instance = nullptr;


struct NavigationScene : public IScene
{
	struct Path
	{
		Vec3 vertices[128];
		float speed;
		int current_index;
		int vertex_count;
		Entity entity;
	};


	NavigationScene(NavigationSystem& system, Universe& universe, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(universe)
		, m_system(system)
		, m_paths(m_allocator)
	{
		m_detail_mesh = nullptr;
		m_polymesh = nullptr;
		m_navquery = nullptr;
		m_navmesh = nullptr;
		m_debug_compact_heightfield = nullptr;
		m_debug_heightfield = nullptr;
		m_debug_contours = nullptr;
	}


	~NavigationScene()
	{
		clear();
	}


	void clear()
	{
		rcFreePolyMeshDetail(m_detail_mesh);
		rcFreePolyMesh(m_polymesh);
		dtFreeNavMeshQuery(m_navquery);
		dtFreeNavMesh(m_navmesh);
		rcFreeCompactHeightfield(m_debug_compact_heightfield);
		rcFreeHeightField(m_debug_heightfield);
		rcFreeContourSet(m_debug_contours);
		m_detail_mesh = nullptr;
		m_polymesh = nullptr;
		m_navquery = nullptr;
		m_navmesh = nullptr;
		m_debug_compact_heightfield = nullptr;
		m_debug_heightfield = nullptr;
		m_debug_contours = nullptr;
	}


	void registerLuaAPI()
	{
		lua_State* L = m_system.m_engine.getState();
		#define REGISTER_FUNCTION(name) \
			do {\
				auto f = &LuaWrapper::wrapMethod<NavigationScene, decltype(&NavigationScene::name), &NavigationScene::name>; \
				LuaWrapper::createSystemFunction(L, "Navigation", #name, f); \
			} while(false) \

		REGISTER_FUNCTION(generateNavmesh);
		REGISTER_FUNCTION(navigate);
		REGISTER_FUNCTION(debugDrawNavmesh);
		REGISTER_FUNCTION(debugDrawCompactHeightfield);
		REGISTER_FUNCTION(debugDrawHeightfield);
		REGISTER_FUNCTION(debugDrawPaths);
		REGISTER_FUNCTION(getPolygonCount);
		REGISTER_FUNCTION(debugDrawContours);

		#undef REGISTER_FUNCTION
	}


	void sendMessage(uint32 type, void*) override
	{
		static const uint32 register_hash = crc32("registerLuaAPI");
		if (type == register_hash) registerLuaAPI();
	}


	void rasterizeGeometry(rcContext& ctx, rcConfig& cfg, rcHeightfield& solid)
	{
		rasterizeMeshes(ctx, cfg, solid);
		rasterizeTerrains(ctx, cfg, solid);
	}


	void rasterizeTerrains(rcContext& ctx, rcConfig& cfg, rcHeightfield& solid)
	{
		const float walkable_threshold = cosf(Math::degreesToRadians(60));

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		ComponentIndex cmp = render_scene->getFirstTerrain();
		while (cmp != INVALID_COMPONENT)
		{
			Entity entity = render_scene->getTerrainEntity(cmp);
			Vec3 pos = m_universe.getPosition(entity);
			Quat rot = m_universe.getRotation(entity);
			Vec2 res = render_scene->getTerrainResolution(cmp);
			float scaleXZ = render_scene->getTerrainXZScale(cmp);
			for (int j = 0; j < (int)res.y - 1; ++j)
			{
				for (int i = 0; i < (int)res.x - 1; ++i)
				{
					float x = i * scaleXZ;
					float z = j * scaleXZ;
					float h0 = render_scene->getTerrainHeightAt(cmp, x, z);
					Vec3 p0 = pos + rot * Vec3(x, h0, z);

					x = (i + 1) * scaleXZ;
					z = j * scaleXZ;
					float h1 = render_scene->getTerrainHeightAt(cmp, x, z);
					Vec3 p1 = pos + rot * Vec3(x, h1, z);

					x = (i + 1) * scaleXZ;
					z = (j + 1) * scaleXZ;
					float h2 = render_scene->getTerrainHeightAt(cmp, x, z);
					Vec3 p2 = pos + rot * Vec3(x, h2, z);

					x = i * scaleXZ;
					z = (j + 1) * scaleXZ;
					float h3 = render_scene->getTerrainHeightAt(cmp, x, z);
					Vec3 p3 = pos + rot * Vec3(x, h3, z);

					Vec3 n = crossProduct(p1 - p0, p0 - p2).normalized();
					uint8 area = n.y > walkable_threshold ? RC_WALKABLE_AREA : 0;
					rcRasterizeTriangle(&ctx, &p0.x, &p1.x, &p2.x, area, solid);

					n = crossProduct(p2 - p0, p0 - p3).normalized();
					area = n.y > walkable_threshold ? RC_WALKABLE_AREA : 0;
					rcRasterizeTriangle(&ctx, &p0.x, &p2.x, &p3.x, area, solid);
				}
			}
			
			cmp = render_scene->getNextTerrain(cmp);
		}
	}


	void rasterizeMeshes(rcContext& ctx, rcConfig& cfg, rcHeightfield& solid)
	{
		const float walkable_threshold = cosf(Math::degreesToRadians(45));

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		for (auto renderable = render_scene->getFirstRenderable(); renderable != INVALID_COMPONENT;
			 renderable = render_scene->getNextRenderable(renderable))
		{
			auto* model = render_scene->getRenderableModel(renderable);
			if (!model) return;
			ASSERT(model->isReady());

			auto& indices = model->getIndices();
			Entity entity = render_scene->getRenderableEntity(renderable);
			Matrix mtx = m_universe.getMatrix(entity);

			for (int mesh_idx = 0; mesh_idx < model->getMeshCount(); ++mesh_idx)
			{
				auto& mesh = model->getMesh(mesh_idx);
				auto* vertices = &model->getVertices()[mesh.attribute_array_offset / mesh.vertex_def.getStride()];
				for (int i = 0; i < mesh.indices_count; i += 3)
				{
					Vec3 a = mtx.multiplyPosition(vertices[indices[mesh.indices_offset + i]]);
					Vec3 b = mtx.multiplyPosition(vertices[indices[mesh.indices_offset + i + 1]]);
					Vec3 c = mtx.multiplyPosition(vertices[indices[mesh.indices_offset + i + 2]]);

					Vec3 n = crossProduct(a - b, a - c).normalized();
					uint8 area = n.y > walkable_threshold ? RC_WALKABLE_AREA : 0;
					rcRasterizeTriangle(&ctx, &a.x, &b.x, &c.x, area, solid);
				}
			}
		}
	}


	void update(float time_delta, bool paused) override
	{
		for(auto& path : m_paths)
		{
			int idx = path.current_index;
			const Vec3& pos = m_universe.getPosition(path.entity);
			Vec3 v = path.vertices[idx] - pos;
			float len = v.length();
			float speed = path.speed;
			if(len < speed * time_delta)
			{
				++path.current_index;
				m_universe.setPosition(path.entity, path.vertices[idx]);
			}
			else
			{
				v *= speed * time_delta / len;
				m_universe.setPosition(path.entity, pos + v);
				v.y = 0;
				v.normalize();
				float wanted_yaw = atan2(v.x, v.z);
				Quat wanted_rot(Vec3(0, 1, 0), wanted_yaw);
				m_universe.setRotation(path.entity, wanted_rot);
			}
		}
		for(int i = m_paths.size() - 1; i >= 0; --i)
		{
			if(m_paths[i].current_index == m_paths[i].vertex_count)
			{
				m_paths.eraseFast(i);
			}
		}
	}


	void debugDrawPaths()
	{
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		const Vec3 OFFSET(0, 0.1f, 0);
		for (auto& path : m_paths)
		{
			for (int i = 1; i < path.vertex_count; ++i)
			{
				render_scene->addDebugLine(path.vertices[i - 1] + OFFSET, path.vertices[i] + OFFSET, 0xffff0000, 0);
			}
		}
	}


	void debugDrawContours()
	{
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		if (!m_debug_contours) return;

		Vec3 orig(-SIZE, -SIZE, -SIZE);
		float cs = m_debug_contours->cs;
		float ch = m_debug_contours->ch;
		for (int i = 0; i < m_debug_contours->nconts; ++i)
		{
			const rcContour& c = m_debug_contours->conts[i];

			if (c.nverts < 2) continue;

			Vec3 first =
				orig + Vec3((float)c.verts[0] * cs, (float)c.verts[1] * ch, (float)c.verts[2] * cs);
			Vec3 prev = first;
			for (int j = 1; j < c.nverts; ++j)
			{
				const int* v = &c.verts[j * 4];
				Vec3 cur = orig + Vec3((float)v[0] * cs, (float)v[1] * ch, (float)v[2] * cs);
				render_scene->addDebugLine(prev, cur, i & 1 ? 0xffff00ff : 0xffff0000, 0);
				prev = cur;
			}

			render_scene->addDebugLine(prev, first, i & 1 ? 0xffff00ff : 0xffff0000, 0);
		}
	}


	void debugDrawHeightfield()
	{
		static const int MAX_CUBES = 2 << 10;

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		if (!m_debug_heightfield) return;

		Vec3 orig(-SIZE, -SIZE, -SIZE);
		int width = m_debug_heightfield->width;
		float cell_size = 0.3f;
		float cell_height = 0.1f;
		int rendered_cubes = 0;
		for(int z = 0; z < m_debug_heightfield->height; ++z)
		{
			for(int x = 0; x < width; ++x)
			{
				float fx = orig.x + x * cell_size;
				float fz = orig.z + z * cell_size;
				const rcSpan* span = m_debug_heightfield->spans[x + z * width];
				while(span)
				{
					char atype = span->area;
					Vec3 mins(fx, orig.y + span->smin * cell_height, fz);
					Vec3 maxs(fx + cell_size, orig.y + span->smax * cell_height, fz + cell_size);
					render_scene->addDebugCubeSolid(mins, maxs, 0xffff00ff, 0);
					render_scene->addDebugCube(mins, maxs, 0xff00aaff, 0);
					span = span->next;
					++rendered_cubes;
					if (rendered_cubes > MAX_CUBES) return;
				}
			}
		}
	}


	void debugDrawCompactHeightfield()
	{
		static const int MAX_CUBES = 2 << 10;

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		if (!m_debug_compact_heightfield) return;

		auto& chf = *m_debug_compact_heightfield;
		const float cs = chf.cs;
		const float ch = chf.ch;

		Vec3 orig(-SIZE, -SIZE, -SIZE);

		int rendered_cubes = 0;
		for (int y = 0; y < chf.height; ++y)
		{
			for (int x = 0; x < chf.width; ++x)
			{
				float vx = orig.x + (float)x * cs;
				float vz = orig.z + (float)y * cs;

				const rcCompactCell& c = chf.cells[x + y * chf.width];

				for (uint32 i = c.index, ni = c.index + c.count; i < ni; ++i)
				{
					float vy = orig.y + float(chf.spans[i].y) * ch;
					render_scene->addDebugTriangle(
						Vec3(vx, vy, vz), Vec3(vx + cs, vy, vz + cs), Vec3(vx + cs, vy, vz), 0xffff00FF, 0);
					render_scene->addDebugTriangle(
						Vec3(vx, vy, vz), Vec3(vx, vy, vz + cs), Vec3(vx + cs, vy, vz + cs), 0xffff00FF, 0);
					++rendered_cubes;
					if (rendered_cubes > MAX_CUBES) return;
				}
			}
		}
	}


	void debugDrawNavmesh()
	{
		if (!m_polymesh) return;
		auto& mesh = *m_polymesh;
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		const int nvp = mesh.nvp;
		const float cs = mesh.cs;
		const float ch = mesh.ch;

		Vec3 color(0, 0, 0);

		for (int idx = 0; idx < mesh.npolys; ++idx)
		{
			const auto* p = &mesh.polys[idx * nvp * 2];

			if (mesh.areas[idx] == RC_WALKABLE_AREA) color.set(0, 0.8f, 1.0f);

			Vec3 vertices[6];
			Vec3* vert = vertices;
			for (int j = 0; j < nvp; ++j)
			{
				if (p[j] == RC_MESH_NULL_IDX) break;

				const auto* v = &mesh.verts[p[j] * 3];
				vert->set(v[0] * cs + mesh.bmin[0], (v[1] + 1) * ch + mesh.bmin[1], v[2] * cs + mesh.bmin[2]);
				++vert;
			}
			for(int i = 2; i < vert - vertices; ++i)
			{
				render_scene->addDebugTriangle(vertices[0], vertices[i-1], vertices[i], 0xff00aaff, 0);
			}
			for(int i = 1; i < vert - vertices; ++i)
			{
				render_scene->addDebugLine(vertices[i], vertices[i-1], 0xff0000ff, 0);
			}
			render_scene->addDebugLine(vertices[0], vertices[vert - vertices - 1], 0xff0000ff, 0);
		}
	}


	void navigate(Entity entity, const Vec3& dest, float speed)
	{
		if (entity == INVALID_ENTITY) return;

		auto& path = m_paths.emplace();
		path.speed = speed;
		path.entity = entity;
		path.current_index = 0;

		dtPolyRef start_poly_ref;
		dtPolyRef end_poly_ref;
		Vec3 start_pos = m_universe.getPosition(entity);

		dtQueryFilter filter;
		dtPolyRef path_poly_ref[256];
		int path_count;
		const float ext[] = {0.1f, 2, 0.1f};
		m_navquery->findNearestPoly(&start_pos.x, ext, &filter, &start_poly_ref, 0);
		m_navquery->findNearestPoly(&dest.x, ext, &filter, &end_poly_ref, 0);
		m_navquery->findPath(
			start_poly_ref, end_poly_ref, &start_pos.x, &dest.x, &filter, path_poly_ref, &path_count, 256);
		dtPolyRef frefs[256];
		unsigned char fflags[256];
		m_navquery->findStraightPath(&start_pos.x,
			&dest.x,
			path_poly_ref,
			path_count,
			&path.vertices[0].x,
			fflags,
			frefs,
			&path.vertex_count,
			256);
	}


	int getPolygonCount()
	{
		if (!m_polymesh) return 0;
		return m_polymesh->npolys;
	}


	bool generateNavmesh()
	{
		clear();

		const float voxel_height = 0.1f;
		const float voxel_size = 0.3f;
		const float agent_height = 2.0f;
		const float agent_radius = 0.6f;
		const float agent_max_step = 0.9f;
		const float agent_max_climb = 1.5f;
		const float detail_sample_dist = 6;
		const float max_edge_length = 12;
		const int min_region_area = 64;
		const int merge_region_area = 400;
		const int max_verts_per_poly = 6;
		const float detail_sample_max_error = 1;

		rcConfig cfg = {};
		cfg.cs = voxel_size;
		cfg.ch = voxel_height;
		cfg.walkableSlopeAngle = 60.0f;
		cfg.walkableHeight = (int)(agent_max_step / cfg.ch + 0.99f);
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
		Vec3 bmin(-SIZE, -SIZE, -SIZE);
		Vec3 bmax(SIZE, SIZE, SIZE);
		rcVcopy(cfg.bmin, &bmin.x);
		rcVcopy(cfg.bmax, &bmax.x);
		rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
		rcHeightfield* solid = rcAllocHeightfield();
		m_debug_heightfield = solid;
		if (!solid)
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Out of memory 'solid'.";
			return false;
		}
		if (!rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not create solid heightfield.";
			return false;
		}
		rasterizeGeometry(ctx, cfg, *solid);

		rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
		rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
		rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

		rcCompactHeightfield* chf = rcAllocCompactHeightfield();
		m_debug_compact_heightfield = chf;
		if (!chf)
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Out of memory 'chf'.";
			return false;
		}

		if (!rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not build compact data.";
			return false;
		}

		if(!m_debug_heightfield) rcFreeHeightField(solid);

		if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not erode.";
			return false;
		}

		if (!rcBuildDistanceField(&ctx, *chf))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not build distance field.";
			return false;
		}

		if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not build regions.";
			return false;
		}

		rcContourSet* cset = rcAllocContourSet();
		m_debug_contours = cset;
		if (!cset)
		{
			ctx.log(RC_LOG_ERROR, "Could not generate navmesh: Out of memory 'cset'.");
			return false;
		}
		if (!rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not create contours.";
			return false;
		}

		m_polymesh = rcAllocPolyMesh();
		if (!m_polymesh)
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Out of memory 'm_polymesh'.";
			return false;
		}
		if (!rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *m_polymesh))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not triangulate contours.";
			return false;
		}

		m_detail_mesh = rcAllocPolyMeshDetail();
		if (!m_detail_mesh)
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Out of memory 'pmdtl'.";
			return false;
		}

		if (!rcBuildPolyMeshDetail(&ctx, *m_polymesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *m_detail_mesh))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not build detail mesh.";
			return false;
		}

		if(!m_debug_compact_heightfield) rcFreeCompactHeightfield(chf);
		if(!m_debug_contours) rcFreeContourSet(cset);

		unsigned char* nav_data = 0;
		int nav_data_size = 0;

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

		if (!dtCreateNavMeshData(&params, &nav_data, &nav_data_size))
		{
			g_log_error.log("Navigation") << "Could not build Detour navmesh.";
			return false;
		}

		m_navmesh = dtAllocNavMesh();
		if (!m_navmesh)
		{
			dtFree(nav_data);
			g_log_error.log("Navigation") << "Could not create Detour navmesh";
			return false;
		}

		dtStatus status;

		status = m_navmesh->init(nav_data, nav_data_size, DT_TILE_FREE_DATA);
		if (dtStatusFailed(status))
		{
			dtFree(nav_data);
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
	Array<Path> m_paths;
	rcCompactHeightfield* m_debug_compact_heightfield;
	rcHeightfield* m_debug_heightfield;
	rcContourSet* m_debug_contours;
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
