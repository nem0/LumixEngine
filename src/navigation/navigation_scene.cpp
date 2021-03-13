#include "navigation_scene.h"
#include "animation/animation_scene.h"
#include "engine/array.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/lumix.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/universe.h"
#include "imgui/IconsFontAwesome5.h"
#include "lua_script/lua_script_system.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include <DetourAlloc.h>
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>


namespace Lumix
{


enum class NavigationSceneVersion : i32 {
	ZONE_GUID,
	LATEST
};


static const ComponentType LUA_SCRIPT_TYPE = reflection::getComponentType("lua_script");
static const ComponentType NAVMESH_ZONE_TYPE = reflection::getComponentType("navmesh_zone");
static const ComponentType NAVMESH_AGENT_TYPE = reflection::getComponentType("navmesh_agent");
static const int CELLS_PER_TILE_SIDE = 256;
static const float CELL_SIZE = 0.3f;


struct RecastZone {
	EntityRef entity;
	NavmeshZone zone;

	u32 m_num_tiles_x = 0;
	u32 m_num_tiles_z = 0;
	dtNavMeshQuery* navquery = nullptr;
	rcPolyMeshDetail* detail_mesh = nullptr;
	rcPolyMesh* polymesh = nullptr;
	dtNavMesh* navmesh = nullptr;
	rcCompactHeightfield* debug_compact_heightfield = nullptr;
	rcHeightfield* debug_heightfield = nullptr;
	rcContourSet* debug_contours = nullptr;
	dtCrowd* crowd = nullptr;
};


struct Agent
{
	enum Flags : u32 {
		MOVE_ENTITY = 1 << 0,
	};

	EntityPtr zone = INVALID_ENTITY;
	EntityRef entity;
	float radius;
	float height;
	int agent;
	bool is_finished;
	u32 flags = 0;
	float speed = 0;
	float yaw_diff = 0;
	float stop_distance = 0;
};


struct NavigationSceneImpl final : NavigationScene
{
	NavigationSceneImpl(Engine& engine, IPlugin& system, Universe& universe, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(universe)
		, m_system(system)
		, m_engine(engine)
		, m_agents(m_allocator)
		, m_zones(m_allocator)
		, m_script_scene(nullptr)
		, m_on_update(m_allocator)
	{
		setGeneratorParams(0.3f, 0.1f, 0.3f, 2.0f, 60.0f, 0.3f);
		m_universe.entityTransformed().bind<&NavigationSceneImpl::onEntityMoved>(this);
	}


	~NavigationSceneImpl()
	{
		m_universe.entityTransformed().unbind<&NavigationSceneImpl::onEntityMoved>(this);
		for(RecastZone& zone : m_zones) {
			clearNavmesh(zone);
		}
	}


	void clear() override
	{
		m_agents.clear();
		m_zones.clear();
	}


	void onEntityMoved(EntityRef entity)
	{
		auto iter = m_agents.find(entity);
		if (!iter.isValid()) return;
		if (m_moving_agent == entity) return;
		if (iter.value().agent < 0) return;
		const Agent& agent = iter.value();
		RecastZone& zone = m_zones[(EntityRef)agent.zone];

		const DVec3 pos = m_universe.getPosition(iter.key());
		const dtCrowdAgent* dt_agent = zone.crowd->getAgent(agent.agent);
		if (squaredLength(pos - *(Vec3*)dt_agent->npos) > 0.1f) {
			const Transform old_zone_tr = m_universe.getTransform(zone.entity);
			const DVec3 target_pos = old_zone_tr.transform(*(Vec3*)dt_agent->targetPos);
			float speed = dt_agent->params.maxSpeed;
			zone.crowd->removeAgent(agent.agent);
			addCrowdAgent(iter.value(), zone);
			if (!agent.is_finished) {
				navigate({entity.index}, target_pos, speed, agent.stop_distance);
			}
		}
	}


	void clearNavmesh(RecastZone& zone) {
		dtFreeNavMeshQuery(zone.navquery);
		rcFreePolyMeshDetail(zone.detail_mesh);
		rcFreePolyMesh(zone.polymesh);
		dtFreeNavMesh(zone.navmesh);
		rcFreeCompactHeightfield(zone.debug_compact_heightfield);
		rcFreeHeightField(zone.debug_heightfield);
		rcFreeContourSet(zone.debug_contours);
		dtFreeCrowd(zone.crowd);
		zone.detail_mesh = nullptr;
		zone.polymesh = nullptr;
		zone.navquery = nullptr;
		zone.navmesh = nullptr;
		zone.debug_compact_heightfield = nullptr;
		zone.debug_heightfield = nullptr;
		zone.debug_contours = nullptr;
		zone.crowd = nullptr;
	}


	void rasterizeGeometry(const Transform& zone_tr, const AABB& aabb, rcContext& ctx, rcConfig& cfg, rcHeightfield& solid)
	{
		rasterizeMeshes(zone_tr, aabb, ctx, cfg, solid);
		rasterizeTerrains(zone_tr, aabb, ctx, cfg, solid);
	}


	void rasterizeTerrains(const Transform& zone_tr, const AABB& zone_aabb, rcContext& ctx, rcConfig& cfg, rcHeightfield& solid)
	{
		PROFILE_FUNCTION();
		const float walkable_threshold = cosf(degreesToRadians(60));

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		EntityPtr entity_ptr = render_scene->getFirstTerrain();
		while (entity_ptr.isValid()) {
			const EntityRef entity = (EntityRef)entity_ptr;
			const Transform terrain_tr = m_universe.getTransform(entity);
			const Transform to_zone = zone_tr.inverted() * terrain_tr;
			const IVec2 res = render_scene->getTerrainResolution(entity);
			float scaleXZ = render_scene->getTerrainXZScale(entity);
			const Transform to_terrain = to_zone.inverted();
			Matrix mtx = to_terrain.rot.toMatrix();
			mtx.setTranslation(Vec3(to_terrain.pos));
			AABB aabb = zone_aabb;
			aabb.transform(mtx);
			const IVec2 from = IVec2(aabb.min.xz() / scaleXZ);
			const IVec2 to = IVec2(aabb.max.xz() / scaleXZ + Vec2(1));
			for (int j = from.y; j < to.y; ++j) {
				for (int i = from.x; i < to.x; ++i) {
					float x = i * scaleXZ;
					float z = j * scaleXZ;

					const float h0 = render_scene->getTerrainHeightAt(entity, x, z);
					const Vec3 p0 = Vec3(to_zone.transform(Vec3(x, h0, z)));

					x = (i + 1) * scaleXZ;
					z = j * scaleXZ;
					const float h1 = render_scene->getTerrainHeightAt(entity, x, z);
					const Vec3 p1 = Vec3(to_zone.transform(Vec3(x, h1, z)));

					x = (i + 1) * scaleXZ;
					z = (j + 1) * scaleXZ;
					const float h2 = render_scene->getTerrainHeightAt(entity, x, z);
					const Vec3 p2 = Vec3(to_zone.transform(Vec3(x, h2, z)));

					x = i * scaleXZ;
					z = (j + 1) * scaleXZ;
					const float h3 = render_scene->getTerrainHeightAt(entity, x, z);
					const Vec3 p3 = Vec3(to_zone.transform(Vec3(x, h3, z)));

					Vec3 n = normalize(cross(p1 - p0, p0 - p2));
					u8 area = n.y > walkable_threshold ? RC_WALKABLE_AREA : 0;
					rcRasterizeTriangle(&ctx, &p0.x, &p1.x, &p2.x, area, solid);

					n = normalize(cross(p2 - p0, p0 - p3));
					area = n.y > walkable_threshold ? RC_WALKABLE_AREA : 0;
					rcRasterizeTriangle(&ctx, &p0.x, &p2.x, &p3.x, area, solid);
				}
			}
			entity_ptr = render_scene->getNextTerrain(entity);
		}
	}


	void rasterizeMeshes(const Transform& zone_tr, const AABB& aabb, rcContext& ctx, rcConfig& cfg, rcHeightfield& solid)
	{
		PROFILE_FUNCTION();
		const float walkable_threshold = cosf(degreesToRadians(45));

		const Transform inv_zone_tr = zone_tr.inverted();

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		u32 no_navigation_flag = Material::getCustomFlag("no_navigation");
		u32 nonwalkable_flag = Material::getCustomFlag("nonwalkable");
		for (EntityPtr model_instance = render_scene->getFirstModelInstance(); 
			model_instance.isValid();
			model_instance = render_scene->getNextModelInstance(model_instance))
		{
			const EntityRef entity = (EntityRef)model_instance;
			auto* model = render_scene->getModelInstanceModel(entity);
			if (!model) return;
			ASSERT(model->isReady());

			const Transform tr = m_universe.getTransform(entity);
			AABB model_aabb = model->getAABB();
			const Transform rel_tr = inv_zone_tr * tr;
			Matrix mtx = rel_tr.rot.toMatrix();
			mtx.setTranslation(Vec3(rel_tr.pos));
			mtx.multiply3x3(rel_tr.scale);
			model_aabb.transform(mtx);
			if (!model_aabb.overlaps(aabb)) continue;

			auto lod = model->getLODIndices()[0];
			for (int mesh_idx = lod.from; mesh_idx <= lod.to; ++mesh_idx) {
				Mesh& mesh = model->getMesh(mesh_idx);
				bool is16 = mesh.areIndices16();

				if (mesh.material->isCustomFlag(no_navigation_flag)) continue;
				bool is_walkable = !mesh.material->isCustomFlag(nonwalkable_flag);
				auto* vertices = &mesh.vertices[0];
				if (is16) {
					const u16* indices16 = (const u16*)mesh.indices.data();
					for (i32 i = 0; i < (i32)mesh.indices.size() / 2; i += 3) {
						Vec3 a = mtx.transformPoint(vertices[indices16[i]]);
						Vec3 b = mtx.transformPoint(vertices[indices16[i + 1]]);
						Vec3 c = mtx.transformPoint(vertices[indices16[i + 2]]);

						Vec3 n = normalize(cross(a - b, a - c));
						u8 area = n.y > walkable_threshold && is_walkable ? RC_WALKABLE_AREA : 0;
						rcRasterizeTriangle(&ctx, &a.x, &b.x, &c.x, area, solid);
					}
				}
				else {
					const u32* indices32 = (const u32*)mesh.indices.data();
					for (i32 i = 0; i < (i32)mesh.indices.size() / 4; i += 3) {
						Vec3 a = mtx.transformPoint(vertices[indices32[i]]);
						Vec3 b = mtx.transformPoint(vertices[indices32[i + 1]]);
						Vec3 c = mtx.transformPoint(vertices[indices32[i + 2]]);

						Vec3 n = normalize(cross(a - b, a - c));
						u8 area = n.y > walkable_threshold && is_walkable ? RC_WALKABLE_AREA : 0;
						rcRasterizeTriangle(&ctx, &a.x, &b.x, &c.x, area, solid);
					}
				}
			}
		}
	}


	void onPathFinished(const Agent& agent)
	{
		if (!m_script_scene) return;
		
		if (!m_universe.hasComponent(agent.entity, LUA_SCRIPT_TYPE)) return;

		for (int i = 0, c = m_script_scene->getScriptCount(agent.entity); i < c; ++i)
		{
			auto* call = m_script_scene->beginFunctionCall(agent.entity, i, "onPathFinished");
			if (!call) continue;

			m_script_scene->endFunctionCall();
		}
	}


	bool isFinished(EntityRef entity) override
	{
		return m_agents[entity].is_finished;
	}


	float getAgentSpeed(EntityRef entity) override
	{
		return m_agents[entity].speed;
	}


	float getAgentYawDiff(EntityRef entity) override
	{
		return m_agents[entity].yaw_diff;
	}


	void update(RecastZone& zone, float time_delta) {
		if (!zone.crowd) return;
		zone.crowd->update(time_delta, nullptr);

		const Transform inv_tr = m_universe.getTransform(zone.entity).inverted();

		for (auto& agent : m_agents) {
			if (agent.agent < 0) continue;
			if (agent.zone != zone.entity) continue;
			
			const dtCrowdAgent* dt_agent = zone.crowd->getAgent(agent.agent);
			//if (dt_agent->paused) continue;

			const Vec3 pos(inv_tr.transform(m_universe.getPosition(agent.entity)));
			const Quat rot = m_universe.getRotation(agent.entity);
			const Vec3 diff = *(Vec3*)dt_agent->npos - pos;

			const Vec3 velocity = *(Vec3*)dt_agent->nvel;
			agent.speed = length(velocity);
			agent.yaw_diff = 0;
			if (squaredLength(velocity) > 0) {
				float wanted_yaw = atan2f(velocity.x, velocity.z);
				float current_yaw = rot.toEuler().y;
				agent.yaw_diff = angleDiff(wanted_yaw, current_yaw);
			}
		}
	}

	void update(float time_delta, bool paused) override {
		PROFILE_FUNCTION();
		if (paused) return;
		if (!m_is_game_running) return;
		
		for (RecastZone& zone : m_zones) {
			update(zone, time_delta);
		}
	}

	void lateUpdate(RecastZone& zone, float time_delta) {
		if (!zone.crowd) return;
		
		const Transform zone_tr = m_universe.getTransform(zone.entity);
		const Transform inv_zone_tr = zone_tr.inverted();

		zone.crowd->doMove(time_delta);

		for (auto& agent : m_agents) {
			if (agent.agent < 0) continue;
			if (agent.zone != zone.entity) continue;

			const dtCrowdAgent* dt_agent = zone.crowd->getAgent(agent.agent);
			//if (dt_agent->paused) continue;

			if (agent.flags & Agent::MOVE_ENTITY) {
				m_moving_agent = agent.entity;
				m_universe.setPosition(agent.entity, zone_tr.transform(*(Vec3*)dt_agent->npos));

				Vec3 vel = *(Vec3*)dt_agent->nvel;
				vel.y = 0;
				float len = length(vel);
				if (len > 0) {
					vel *= 1 / len;
					float angle = atan2f(vel.x, vel.z);
					Quat wanted_rot(Vec3(0, 1, 0), angle);
					Quat old_rot = m_universe.getRotation(agent.entity);
					Quat new_rot = nlerp(wanted_rot, old_rot, 0.90f);
					m_universe.setRotation(agent.entity, new_rot);
				}
			}
			else {
				*(Vec3*)dt_agent->npos = Vec3(zone_tr.inverted().transform(m_universe.getPosition(agent.entity)));
			}

			if (dt_agent->ncorners == 0 && dt_agent->targetState != DT_CROWDAGENT_TARGET_REQUESTING) {
				if (!agent.is_finished) {
					zone.crowd->resetMoveTarget(agent.agent);
					agent.is_finished = true;
					onPathFinished(agent);
				}
			}
			else if (dt_agent->ncorners == 1 && agent.stop_distance > 0) {
				Vec3 diff = *(Vec3*)dt_agent->targetPos - *(Vec3*)dt_agent->npos;
				if (squaredLength(diff) < agent.stop_distance * agent.stop_distance) {
					zone.crowd->resetMoveTarget(agent.agent);
					agent.is_finished = true;
					onPathFinished(agent);
				}
			}
			else {
				agent.is_finished = false;
			}
			m_moving_agent = INVALID_ENTITY;
		}
	}

	void lateUpdate(float time_delta, bool paused) override {
		PROFILE_FUNCTION();
		if (paused) return;
		if (!m_is_game_running) return;

		for (RecastZone& zone : m_zones) {
			lateUpdate(zone, time_delta);
		}
	}

	static float distancePtLine2d(const float* pt, const float* p, const float* q)
	{
		float pqx = q[0] - p[0];
		float pqz = q[2] - p[2];
		float dx = pt[0] - p[0];
		float dz = pt[2] - p[2];
		float d = pqx*pqx + pqz*pqz;
		float t = pqx*dx + pqz*dz;
		if (d != 0) t /= d;
		dx = p[0] + t*pqx - pt[0];
		dz = p[2] + t*pqz - pt[2];
		return dx*dx + dz*dz;
	}


	static void drawPoly(RenderScene* render_scene, const Transform& tr, const dtMeshTile& tile, const dtPoly& poly)
	{
		const unsigned int ip = (unsigned int)(&poly - tile.polys);
		const dtPolyDetail& pd = tile.detailMeshes[ip];

		for (int i = 0; i < pd.triCount; ++i)
		{
			Vec3 v[3];
			const unsigned char* t = &tile.detailTris[(pd.triBase + i) * 4];
			for (int k = 0; k < 3; ++k)
			{
				if (t[k] < poly.vertCount)
				{
					v[k] = *(Vec3*)&tile.verts[poly.verts[t[k]] * 3];
				}
				else
				{
					v[k] = *(Vec3*)&tile.detailVerts[(pd.vertBase + t[k] - poly.vertCount) * 3];
				}
			}
			render_scene->addDebugTriangle(tr.transform(v[0]), tr.transform(v[1]), tr.transform(v[2]), 0xff00aaff);
		}

		for (int k = 0; k < pd.triCount; ++k)
		{
			const unsigned char* t = &tile.detailTris[(pd.triBase + k) * 4];
			const float* tv[3];
			for (int m = 0; m < 3; ++m)
			{
				if (t[m] < poly.vertCount)
					tv[m] = &tile.verts[poly.verts[t[m]] * 3];
				else
					tv[m] = &tile.detailVerts[(pd.vertBase + (t[m] - poly.vertCount)) * 3];
			}
			for (int m = 0, n = 2; m < 3; n = m++)
			{
				if (((t[3] >> (n * 2)) & 0x3) == 0) continue; // Skip inner detail edges.
				render_scene->addDebugLine(tr.transform(*(Vec3*)tv[n]), tr.transform(*(Vec3*)tv[m]), 0xff0000ff);
			}
		}
	}


	const dtCrowdAgent* getDetourAgent(EntityRef entity) override
	{
		auto iter = m_agents.find(entity);
		if (!iter.isValid()) return nullptr;
		
		const Agent& agent = iter.value();
		if (agent.agent < 0) return nullptr;
		if (!agent.zone.isValid()) return nullptr;

		auto zone_iter = m_zones.find((EntityRef)agent.zone);
		if (!zone_iter.isValid()) return nullptr;

		dtCrowd* crowd = zone_iter.value().crowd;

		if (!crowd) return nullptr;

		return crowd->getAgent(agent.agent);
	}


	void debugDrawPath(EntityRef entity) override
	{
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		
		auto agent_iter = m_agents.find(entity);
		if (!agent_iter.isValid()) return;

		const Agent& agent = agent_iter.value();
		if (agent.agent < 0) return;

		const RecastZone& zone = m_zones[(EntityRef)agent.zone];
		if (!zone.crowd) return;

		const Transform zone_tr = m_universe.getTransform(zone.entity);
		const dtCrowdAgent* dt_agent = zone.crowd->getAgent(agent.agent);

		const dtPolyRef* path = dt_agent->corridor.getPath();
		const int npath = dt_agent->corridor.getPathCount();
		for (int j = 0; j < npath; ++j) {
			dtPolyRef ref = path[j];
			const dtMeshTile* tile = nullptr;
			const dtPoly* poly = nullptr;
			if (dtStatusFailed(zone.navmesh->getTileAndPolyByRef(ref, &tile, &poly))) continue;

			drawPoly(render_scene, zone_tr, *tile, *poly);
		}

		Vec3 prev = *(Vec3*)dt_agent->npos;
		for (int i = 0; i < dt_agent->ncorners; ++i) {
			Vec3 tmp = *(Vec3*)&dt_agent->cornerVerts[i * 3];
			render_scene->addDebugLine(zone_tr.transform(prev), zone_tr.transform(tmp), 0xffff0000);
			prev = tmp;
		}
		render_scene->addDebugCross(zone_tr.transform(*(Vec3*)dt_agent->targetPos), 1.0f, 0xffffffff);
		const Vec3 vel = *(Vec3*)dt_agent->vel;
		const DVec3 pos = m_universe.getPosition(entity);
		render_scene->addDebugLine(pos, pos + zone_tr.rot.rotate(vel), 0xff0000ff);
	}


	bool hasDebugDrawData(EntityRef zone) const override
	{
		return m_zones[zone].debug_contours != nullptr;
	}


	void debugDrawContours(EntityRef zone_entity) override {
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		const RecastZone& zone = m_zones[zone_entity];
		if (!zone.debug_contours) return;

		const Transform tr = m_universe.getTransform(zone_entity);

		Vec3 orig = m_debug_tile_origin;
		float cs = zone.debug_contours->cs;
		float ch = zone.debug_contours->ch;
		for (int i = 0; i < zone.debug_contours->nconts; ++i) {
			const rcContour& c = zone.debug_contours->conts[i];

			if (c.nverts < 2) continue;

			Vec3 first =
				orig + Vec3((float)c.verts[0] * cs, (float)c.verts[1] * ch, (float)c.verts[2] * cs);
			Vec3 prev = first;
			for (int j = 1; j < c.nverts; ++j) {
				const int* v = &c.verts[j * 4];
				Vec3 cur = orig + Vec3((float)v[0] * cs, (float)v[1] * ch, (float)v[2] * cs);
				render_scene->addDebugLine(tr.transform(prev), tr.transform(cur), i & 1 ? 0xffff00ff : 0xffff0000);
				prev = cur;
			}

			render_scene->addDebugLine(tr.transform(prev), tr.transform(first), i & 1 ? 0xffff00ff : 0xffff0000);
		}
	}

	bool isNavmeshReady(EntityRef zone) const override { return m_zones[zone].navmesh != nullptr; }

	struct LoadCallback {
		LoadCallback(NavigationSceneImpl& scene, EntityRef entity)
			: scene(scene)
			, entity(entity)
		{}

		void fileLoaded(u64 size, const u8* mem, bool success) {
			auto iter = scene.m_zones.find(entity);
			if (!iter.isValid()) {
				LUMIX_DELETE(scene.m_allocator, this);
				return;
			}

			if (!success) {
				logError("Could not load navmesh");
				LUMIX_DELETE(scene.m_allocator, this);
				return;
			}

			RecastZone& zone = iter.value();
			if (!scene.initNavmesh(zone)) {
				LUMIX_DELETE(scene.m_allocator, this);
				return;
			}

			InputMemoryStream file(mem, size);
			file.read(zone.m_num_tiles_x);
			file.read(zone.m_num_tiles_z);
			dtNavMeshParams params;
			file.read(&params, sizeof(params));
			if (dtStatusFailed(zone.navmesh->init(&params))) {
				logError("Could not init Detour navmesh");
				LUMIX_DELETE(scene.m_allocator, this);
				return;
			}
			for (u32 j = 0; j < zone.m_num_tiles_z; ++j) {
				for (u32 i = 0; i < zone.m_num_tiles_x; ++i) {
					int data_size;
					file.read(&data_size, sizeof(data_size));
					u8* data = (u8*)dtAlloc(data_size, DT_ALLOC_PERM);
					file.read(data, data_size);
					if (dtStatusFailed(zone.navmesh->addTile(data, data_size, DT_TILE_FREE_DATA, 0, 0))) {
						dtFree(data);
						LUMIX_DELETE(scene.m_allocator, this);
						return;
					}
				}
			}

			if (!zone.crowd) scene.initCrowd(zone);

			LUMIX_DELETE(scene.m_allocator, this);
		}

		NavigationSceneImpl& scene;
		EntityRef entity;
	};

	bool loadZone(EntityRef zone_entity) override {
		RecastZone& zone = m_zones[zone_entity];
		clearNavmesh(zone);

		LoadCallback* lcb = LUMIX_NEW(m_allocator, LoadCallback)(*this, zone_entity);

		StaticString<LUMIX_MAX_PATH> path("universes/navzones/", zone.zone.guid, ".nav");
		FileSystem::ContentCallback cb;
		cb.bind<&LoadCallback::fileLoaded>(lcb);
		FileSystem& fs = m_engine.getFileSystem();
		return fs.getContent(Path(path), cb).isValid();
	}

	bool saveZone(EntityRef zone_entity) override {
		RecastZone& zone = m_zones[zone_entity];
		if (!zone.navmesh) return false;

		FileSystem& fs = m_engine.getFileSystem();
		
		os::OutputFile file;
		StaticString<LUMIX_MAX_PATH> path("universes/navzones/", zone.zone.guid, ".nav");
		if (!fs.open(path, file)) return false;

		bool success = file.write(zone.m_num_tiles_x);
		success = success && file.write(zone.m_num_tiles_z);
		const dtNavMeshParams* params = zone.navmesh->getParams();
		success = success && file.write(params, sizeof(*params));
		for (u32 j = 0; j < zone.m_num_tiles_z; ++j) {
			for (u32 i = 0; i < zone.m_num_tiles_x; ++i) {
				const auto* tile = zone.navmesh->getTileAt(i, j, 0);
				success = success && file.write(&tile->dataSize, sizeof(tile->dataSize));
				success = success && file.write(tile->data, tile->dataSize);
			}
		}

		file.close();
		return success;
	}


	void debugDrawHeightfield(EntityRef zone_entity) override {
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		
		const RecastZone& zone = m_zones[zone_entity];
		if (!zone.debug_heightfield) return;

		const Transform tr = m_universe.getTransform(zone_entity);

		Vec3 orig = m_debug_tile_origin;
		int width = zone.debug_heightfield->width;
		float cell_height = 0.1f;
		for(int z = 0; z < zone.debug_heightfield->height; ++z) {
			for(int x = 0; x < width; ++x) {
				float fx = orig.x + x * CELL_SIZE;
				float fz = orig.z + z * CELL_SIZE;
				const rcSpan* span = zone.debug_heightfield->spans[x + z * width];
				while(span) {
					Vec3 mins(fx, orig.y + span->smin * cell_height, fz);
					Vec3 maxs(fx + CELL_SIZE, orig.y + span->smax * cell_height, fz + CELL_SIZE);
					u32 color = span->area == 0 ? 0xffff0000 : 0xff00aaff;
					render_scene->addDebugCubeSolid(tr.transform(mins), tr.transform(maxs), color);
					render_scene->addDebugCube(tr.transform(mins), tr.transform(maxs), 0xffffFFFF);
					span = span->next;
				}
			}
		}
	}


	void debugDrawCompactHeightfield(EntityRef zone_entity) override {
		static const int MAX_CUBES = 0xffFF;

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		
		const RecastZone& zone = m_zones[zone_entity];
		if (!zone.debug_compact_heightfield) return;

		const Transform tr = m_universe.getTransform(zone_entity);

		auto& chf = *zone.debug_compact_heightfield;
		const float cs = chf.cs;
		const float ch = chf.ch;

		Vec3 orig = m_debug_tile_origin;

		int rendered_cubes = 0;
		for (int y = 0; y < chf.height; ++y) {
			for (int x = 0; x < chf.width; ++x) {
				float vx = orig.x + (float)x * cs;
				float vz = orig.z + (float)y * cs;

				const rcCompactCell& c = chf.cells[x + y * chf.width];

				for (u32 i = c.index, ni = c.index + c.count; i < ni; ++i) {
					float vy = orig.y + float(chf.spans[i].y) * ch;
					render_scene->addDebugTriangle(tr.transform(Vec3(vx, vy, vz))
						, tr.transform(Vec3(vx + cs, vy, vz + cs))
						, tr.transform(Vec3(vx + cs, vy, vz))
						, 0xffff00FF);
					render_scene->addDebugTriangle(tr.transform(Vec3(vx, vy, vz))
						, tr.transform(Vec3(vx, vy, vz + cs))
						, tr.transform(Vec3(vx + cs, vy, vz + cs))
						, 0xffff00FF);
					++rendered_cubes;
					if (rendered_cubes > MAX_CUBES) return;
				}
			}
		}
	}


	static void drawPolyBoundaries(RenderScene* render_scene,
		const Transform& tr,
		const dtMeshTile& tile,
		const unsigned int col,
		bool inner)
	{
		static const float thr = 0.01f * 0.01f;

		for (int i = 0; i < tile.header->polyCount; ++i)
		{
			const dtPoly* p = &tile.polys[i];

			if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;

			const dtPolyDetail* pd = &tile.detailMeshes[i];

			for (int j = 0, nj = (int)p->vertCount; j < nj; ++j)
			{
				unsigned int c = col;
				if (inner)
				{
					if (p->neis[j] == 0) continue;
					if (p->neis[j] & DT_EXT_LINK)
					{
						bool con = false;
						for (unsigned int k = p->firstLink; k != DT_NULL_LINK; k = tile.links[k].next)
						{
							if (tile.links[k].edge == j)
							{
								con = true;
								break;
							}
						}
						if (con)
							c = 0xffffffff;
						else
							c = 0xff000000;
					}
					else
						c = 0xff004466;
				}
				else
				{
					if (p->neis[j] != 0) continue;
				}

				const float* v0 = &tile.verts[p->verts[j] * 3];
				const float* v1 = &tile.verts[p->verts[(j + 1) % nj] * 3];

				// Draw detail mesh edges which align with the actual poly edge.
				// This is really slow.
				for (int k = 0; k < pd->triCount; ++k)
				{
					const unsigned char* t = &tile.detailTris[(pd->triBase + k) * 4];
					const float* tv[3];
					for (int m = 0; m < 3; ++m)
					{
						if (t[m] < p->vertCount)
							tv[m] = &tile.verts[p->verts[t[m]] * 3];
						else
							tv[m] = &tile.detailVerts[(pd->vertBase + (t[m] - p->vertCount)) * 3];
					}
					for (int m = 0, n = 2; m < 3; n = m++)
					{
						if (((t[3] >> (n * 2)) & 0x3) == 0) continue; // Skip inner detail edges.
						if (distancePtLine2d(tv[n], v0, v1) < thr && distancePtLine2d(tv[m], v0, v1) < thr)
						{
							render_scene->addDebugLine(tr.transform(*(Vec3*)tv[n] + Vec3(0, 0.5f, 0))
								, tr.transform(*(Vec3*)tv[m] + Vec3(0, 0.5f, 0))
								, c);
						}
					}
				}
			}
		}
	}

	static void drawTilePortal(RenderScene* render_scene, const Transform& zone_tr, const dtMeshTile& tile) {
		const float padx = 0.04f;
		const float pady = tile.header->walkableClimb;

		for (int side = 0; side < 8; ++side) {
			unsigned short m = DT_EXT_LINK | (unsigned short)side;

			for (int i = 0; i < tile.header->polyCount; ++i) {
				dtPoly* poly = &tile.polys[i];

				const int nv = poly->vertCount;
				for (int j = 0; j < nv; ++j) {
					if (poly->neis[j] != m) continue;

					const float* va = &tile.verts[poly->verts[j] * 3];
					const float* vb = &tile.verts[poly->verts[(j + 1) % nv] * 3];

					if (side == 0 || side == 4) {
						unsigned int col = side == 0 ? 0xff0000aa : 0xff00aaaa;

						const float x = va[0] + ((side == 0) ? -padx : padx);

						render_scene->addDebugLine(zone_tr.transform(Vec3(x, va[1] - pady, va[2])), zone_tr.transform(Vec3(x, va[1] + pady, va[2])), col);
						render_scene->addDebugLine(zone_tr.transform(Vec3(x, va[1] + pady, va[2])), zone_tr.transform(Vec3(x, vb[1] + pady, vb[2])), col);
						render_scene->addDebugLine(zone_tr.transform(Vec3(x, vb[1] + pady, vb[2])), zone_tr.transform(Vec3(x, vb[1] - pady, vb[2])), col);
						render_scene->addDebugLine(zone_tr.transform(Vec3(x, vb[1] - pady, vb[2])), zone_tr.transform(Vec3(x, va[1] - pady, va[2])), col);
					}
					else if (side == 2 || side == 6) {
						unsigned int col = side == 2 ? 0xff00aa00 : 0xffaaaa00;

						const float z = va[2] + ((side == 2) ? -padx : padx);

						render_scene->addDebugLine(zone_tr.transform(Vec3(va[0], va[1] - pady, z)), zone_tr.transform(Vec3(va[0], va[1] + pady, z)), col);
						render_scene->addDebugLine(zone_tr.transform(Vec3(va[0], va[1] + pady, z)), zone_tr.transform(Vec3(vb[0], vb[1] + pady, z)), col);
						render_scene->addDebugLine(zone_tr.transform(Vec3(vb[0], vb[1] + pady, z)), zone_tr.transform(Vec3(vb[0], vb[1] - pady, z)), col);
						render_scene->addDebugLine(zone_tr.transform(Vec3(vb[0], vb[1] - pady, z)), zone_tr.transform(Vec3(va[0], va[1] - pady, z)), col);
					}
				}
			}
		}
	}


	void debugDrawNavmesh(EntityRef zone_entity, const DVec3& world_pos, bool inner_boundaries, bool outer_boundaries, bool portals) override
	{
		const RecastZone& zone = m_zones[zone_entity];
		if (!zone.navmesh) return;

		const Transform tr = m_universe.getTransform(zone_entity);
		const Vec3 pos(tr.inverted().transform(world_pos));

		const Vec3 min = -zone.zone.extents;
		const Vec3 max = zone.zone.extents;
		if (pos.x > max.x || pos.x < min.x || pos.z > max.z || pos.z < min.z) return;

		int x = int((pos.x - min.x + (1 + m_config.borderSize) * m_config.cs) / (CELLS_PER_TILE_SIDE * CELL_SIZE));
		int z = int((pos.z - min.z + (1 + m_config.borderSize) * m_config.cs) / (CELLS_PER_TILE_SIDE * CELL_SIZE));
		const dtMeshTile* tile = zone.navmesh->getTileAt(x, z, 0);
		if (!tile) return;

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		for (int i = 0; i < tile->header->polyCount; ++i) {
			const dtPoly* p = &tile->polys[i];
			if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
			drawPoly(render_scene, tr, *tile, *p);
		}

		if (outer_boundaries) drawPolyBoundaries(render_scene, tr, *tile, 0xffff0000, false);
		if (inner_boundaries) drawPolyBoundaries(render_scene, tr, *tile, 0xffff0000, true);

		if (portals) drawTilePortal(render_scene, tr, *tile);
	}


	void stopGame() override
	{
		m_is_game_running = false;
		for (RecastZone& zone : m_zones) {
			if (zone.crowd) {
				for (Agent& agent : m_agents) {
					if (agent.zone == zone.entity) {
						zone.crowd->removeAgent(agent.agent);
						agent.agent = -1;
					}
				}
				dtFreeCrowd(zone.crowd);
				zone.crowd = nullptr;
			}
		}
	}


	void startGame() override
	{
		m_is_game_running = true;
		auto* scene = m_universe.getScene(crc32("lua_script"));
		m_script_scene = static_cast<LuaScriptScene*>(scene);
		
		for (RecastZone& zone : m_zones) {
			if (zone.navmesh && !zone.crowd) initCrowd(zone);
		}
	}


	bool initCrowd(RecastZone& zone) {
		ASSERT(!zone.crowd);

		zone.crowd = dtAllocCrowd();
		if (!zone.crowd->init(1000, 4.0f, zone.navmesh)) {
			dtFreeCrowd(zone.crowd);
			zone.crowd = nullptr;
			return false;
		}

		const Transform inv_zone_tr = m_universe.getTransform(zone.entity).inverted();
		const Vec3 min = -zone.zone.extents;
		const Vec3 max = zone.zone.extents;

		for (auto iter = m_agents.begin(), end = m_agents.end(); iter != end; ++iter) {
			Agent& agent = iter.value();
			if (agent.zone.isValid()) continue;

			const Vec3 pos = Vec3(inv_zone_tr.transform(m_universe.getPosition(agent.entity)));
			if (pos.x > min.x && pos.y > min.y && pos.z > min.z 
				&& pos.x < max.x && pos.y < max.y && pos.z < max.z)
			{
				agent.zone = zone.entity;
				addCrowdAgent(agent, zone);
			}
		}
		return true;
	}

	RecastZone* getZone(const Agent& agent) {
		if (!agent.zone.isValid()) return nullptr;
		return &m_zones[(EntityRef)agent.zone];
	}

	void cancelNavigation(EntityRef entity) override {
		auto iter = m_agents.find(entity);
		if (iter == m_agents.end()) return;

		Agent& agent = iter.value();
		if (agent.agent < 0) return;
		
		RecastZone* zone = getZone(agent);

		if (zone) {
			zone->crowd->resetMoveTarget(agent.agent);
		}
	}


	void setActorActive(EntityRef entity, bool active) override
	{
		/*if (!m_crowd) return;

		auto iter = m_agents.find(entity);
		if (iter == m_agents.end()) return;

		Agent& agent = iter.value();
		if (agent.agent < 0) return;

		dtCrowdAgent* dt_agent = m_crowd->getEditableAgent(agent.agent);
		if (dt_agent) dt_agent->paused = !active;*/
		// TOOD
	}


	bool navigate(EntityRef entity, const DVec3& world_dest, float speed, float stop_distance) override
	{
		auto iter = m_agents.find(entity);
		if (iter == m_agents.end()) return false;
		
		Agent& agent = iter.value();
		if (agent.agent < 0) return false;
		if (!agent.zone.isValid()) return false;

		RecastZone& zone = m_zones[(EntityRef)agent.zone];

		if (!zone.navquery) return false;
		if (!zone.crowd) return false;

		dtPolyRef end_poly_ref;
		dtQueryFilter filter;
		static const float ext[] = { 1.0f, 20.0f, 1.0f };

		const Transform zone_tr = m_universe.getTransform(zone.entity);
		const Vec3 dest = Vec3(zone_tr.inverted().transform(world_dest));

		zone.navquery->findNearestPoly(&dest.x, ext, &filter, &end_poly_ref, 0);
		dtCrowdAgentParams params = zone.crowd->getAgent(agent.agent)->params;
		params.maxSpeed = speed;
		zone.crowd->updateAgentParameters(agent.agent, &params);
		if (zone.crowd->requestMoveTarget(agent.agent, end_poly_ref, &dest.x)) {
			agent.stop_distance = stop_distance;
			agent.is_finished = false;
		}
		else {
			logError("requestMoveTarget failed");
			agent.is_finished = true;
		}
		return !agent.is_finished;
	}


	void setGeneratorParams(float cell_size,
		float cell_height,
		float agent_radius,
		float agent_height,
		float walkable_angle,
		float max_climb) override
	{
		static const float DETAIL_SAMPLE_DIST = 6;
		static const float DETAIL_SAMPLE_MAX_ERROR = 1;

		m_config.cs = cell_size;
		m_config.ch = cell_height;
		m_config.walkableSlopeAngle = walkable_angle;
		m_config.walkableHeight = (int)(agent_height / m_config.ch + 0.99f);
		m_config.walkableClimb = (int)(max_climb / m_config.ch);
		m_config.walkableRadius = (int)(agent_radius / m_config.cs + 0.99f);
		m_config.maxEdgeLen = (int)(12 / m_config.cs);
		m_config.maxSimplificationError = 1.3f;
		m_config.minRegionArea = 8 * 8;
		m_config.mergeRegionArea = 20 * 20;
		m_config.maxVertsPerPoly = 6;
		m_config.detailSampleDist = DETAIL_SAMPLE_DIST < 0.9f ? 0 : CELL_SIZE * DETAIL_SAMPLE_DIST;
		m_config.detailSampleMaxError = m_config.ch * DETAIL_SAMPLE_MAX_ERROR;
		m_config.borderSize = m_config.walkableRadius + 3;
		m_config.tileSize = CELLS_PER_TILE_SIDE;
		m_config.width = m_config.tileSize + m_config.borderSize * 2;
		m_config.height = m_config.tileSize + m_config.borderSize * 2;
	}


	bool generateTileAt(EntityRef zone_entity, const DVec3& world_pos, bool keep_data) override {
		RecastZone& zone = m_zones[zone_entity];
		const Transform tr = m_universe.getTransform(zone_entity);
		const Vec3 pos = Vec3(tr.inverted().transform(world_pos));
		const Vec3 min = -zone.zone.extents;
		const int x = int((pos.x - min.x + (1 + m_config.borderSize) * m_config.cs) / (CELLS_PER_TILE_SIDE * CELL_SIZE));
		const int z = int((pos.z - min.z + (1 + m_config.borderSize) * m_config.cs) / (CELLS_PER_TILE_SIDE * CELL_SIZE));
		return generateTile(zone, zone_entity, x, z, keep_data);
	}

	bool generateTile(RecastZone& zone, EntityRef zone_entity, int x, int z, bool keep_data) {
		PROFILE_FUNCTION();
		if (!zone.navmesh) return false;

		zone.navmesh->removeTile(zone.navmesh->getTileRefAt(x, z, 0), 0, 0);

		rcContext ctx;
		const Vec3 min = -zone.zone.extents;
		const Vec3 max = zone.zone.extents;
		Vec3 bmin(min.x + x * CELLS_PER_TILE_SIDE * CELL_SIZE - (1 + m_config.borderSize) * m_config.cs,
			min.y,
			min.z + z * CELLS_PER_TILE_SIDE * CELL_SIZE - (1 + m_config.borderSize) * m_config.cs);
		Vec3 bmax(bmin.x + CELLS_PER_TILE_SIDE * CELL_SIZE + (1 + m_config.borderSize) * m_config.cs,
			max.y,
			bmin.z + CELLS_PER_TILE_SIDE * CELL_SIZE + (1 + m_config.borderSize) * m_config.cs);
		if (keep_data) m_debug_tile_origin = bmin;
		rcVcopy(m_config.bmin, &bmin.x);
		rcVcopy(m_config.bmax, &bmax.x);
		rcHeightfield* solid = rcAllocHeightfield();
		zone.debug_heightfield = keep_data ? solid : nullptr;
		if (!solid) {
			logError("Could not generate navmesh: Out of memory 'solid'.");
			return false;
		}

		if (!rcCreateHeightfield(
				&ctx, *solid, m_config.width, m_config.height, m_config.bmin, m_config.bmax, m_config.cs, m_config.ch))
		{
			logError("Could not generate navmesh: Could not create solid heightfield.");
			return false;
		}

		const Transform tr = m_universe.getTransform(zone_entity);
		rasterizeGeometry(tr, AABB(bmin, bmax), ctx, m_config, *solid);

		rcFilterLowHangingWalkableObstacles(&ctx, m_config.walkableClimb, *solid);
		rcFilterLedgeSpans(&ctx, m_config.walkableHeight, m_config.walkableClimb, *solid);
		rcFilterWalkableLowHeightSpans(&ctx, m_config.walkableHeight, *solid);

		rcCompactHeightfield* chf = rcAllocCompactHeightfield();
		zone.debug_compact_heightfield = keep_data ? chf : nullptr;
		if (!chf) {
			logError("Could not generate navmesh: Out of memory 'chf'.");
			return false;
		}

		if (!rcBuildCompactHeightfield(&ctx, m_config.walkableHeight, m_config.walkableClimb, *solid, *chf)) {
			logError("Could not generate navmesh: Could not build compact data.");
			return false;
		}

		if (!zone.debug_heightfield) rcFreeHeightField(solid);

		if (!rcErodeWalkableArea(&ctx, m_config.walkableRadius, *chf)) {
			logError("Could not generate navmesh: Could not erode.");
			return false;
		}

		if (!rcBuildDistanceField(&ctx, *chf)) {
			logError("Could not generate navmesh: Could not build distance field.");
			return false;
		}

		if (!rcBuildRegions(&ctx, *chf, m_config.borderSize, m_config.minRegionArea, m_config.mergeRegionArea)) {
			logError("Could not generate navmesh: Could not build regions.");
			return false;
		}

		rcContourSet* cset = rcAllocContourSet();
		zone.debug_contours = keep_data ? cset : nullptr;
		if (!cset) {
			ctx.log(RC_LOG_ERROR, "Could not generate navmesh: Out of memory 'cset'.");
			return false;
		}

		if (!rcBuildContours(&ctx, *chf, m_config.maxSimplificationError, m_config.maxEdgeLen, *cset)) {
			logError("Could not generate navmesh: Could not create contours.");
			return false;
		}

		zone.polymesh = rcAllocPolyMesh();
		if (!zone.polymesh) {
			logError("Could not generate navmesh: Out of memory 'm_polymesh'.");
			return false;
		}
		if (!rcBuildPolyMesh(&ctx, *cset, m_config.maxVertsPerPoly, *zone.polymesh)) {
			logError("Could not generate navmesh: Could not triangulate contours.");
			return false;
		}

		zone.detail_mesh = rcAllocPolyMeshDetail();
		if (!zone.detail_mesh) {
			logError("Could not generate navmesh: Out of memory 'pmdtl'.");
			return false;
		}

		if (!rcBuildPolyMeshDetail(
				&ctx, *zone.polymesh, *chf, m_config.detailSampleDist, m_config.detailSampleMaxError, *zone.detail_mesh))
		{
			logError("Could not generate navmesh: Could not build detail mesh.");
			return false;
		}

		if (!zone.debug_compact_heightfield) rcFreeCompactHeightfield(chf);
		if (!zone.debug_contours) rcFreeContourSet(cset);

		unsigned char* nav_data = 0;
		int nav_data_size = 0;

		for (int i = 0; i < zone.polymesh->npolys; ++i) {
			zone.polymesh->flags[i] = zone.polymesh->areas[i] == RC_WALKABLE_AREA ? 1 : 0;
		}

		dtNavMeshCreateParams params = {};
		params.verts = zone.polymesh->verts;
		params.vertCount = zone.polymesh->nverts;
		params.polys = zone.polymesh->polys;
		params.polyAreas = zone.polymesh->areas;
		params.polyFlags = zone.polymesh->flags;
		params.polyCount = zone.polymesh->npolys;
		params.nvp = zone.polymesh->nvp;
		params.detailMeshes = zone.detail_mesh->meshes;
		params.detailVerts = zone.detail_mesh->verts;
		params.detailVertsCount = zone.detail_mesh->nverts;
		params.detailTris = zone.detail_mesh->tris;
		params.detailTriCount = zone.detail_mesh->ntris;
		params.walkableHeight = m_config.walkableHeight * m_config.ch;
		params.walkableRadius = m_config.walkableRadius * m_config.cs;
		params.walkableClimb = m_config.walkableClimb * m_config.ch;
		params.tileX = x;
		params.tileY = z;
		rcVcopy(params.bmin, zone.polymesh->bmin);
		rcVcopy(params.bmax, zone.polymesh->bmax);
		params.cs = m_config.cs;
		params.ch = m_config.ch;
		params.buildBvTree = false;

		if (!dtCreateNavMeshData(&params, &nav_data, &nav_data_size)) {
			logError("Could not build Detour navmesh.");
			return false;
		}

		if (dtStatusFailed(zone.navmesh->addTile(nav_data, nav_data_size, DT_TILE_FREE_DATA, 0, nullptr))) {
			logError("Could not add Detour tile.");
			return false;
		}
		return true;
	}


	bool initNavmesh(RecastZone& zone) {
		ASSERT(!zone.navmesh);

		zone.navmesh = dtAllocNavMesh();
		if (!zone.navmesh) {
			logError("Could not create Detour navmesh");
			return false;
		}

		zone.navquery = dtAllocNavMeshQuery();
		if (!zone.navquery) {
			logError("Could not create Detour navmesh query");
			return false;
		}

		if (dtStatusFailed(zone.navquery->init(zone.navmesh, 2048))) {
			logError("Could not init Detour navmesh query");
			return false;
		}
		return true;
	}

	bool generateNavmesh(EntityRef zone_entity) override {
		PROFILE_FUNCTION();
		RecastZone& zone =  m_zones[zone_entity];
		clearNavmesh(zone);

		if (!initNavmesh(zone)) return false;

		dtNavMeshParams params;
		const Vec3 min = -zone.zone.extents;
		const Vec3 max = zone.zone.extents;
			
		rcVcopy(params.orig, &min.x);
		params.tileWidth = float(CELLS_PER_TILE_SIDE * CELL_SIZE);
		params.tileHeight = float(CELLS_PER_TILE_SIDE * CELL_SIZE);
		int grid_width, grid_height;
		rcCalcGridSize(&min.x, &max.x, CELL_SIZE, &grid_width, &grid_height);
		zone.m_num_tiles_x = (grid_width + CELLS_PER_TILE_SIDE - 1) / CELLS_PER_TILE_SIDE;
		zone.m_num_tiles_z = (grid_height + CELLS_PER_TILE_SIDE - 1) / CELLS_PER_TILE_SIDE;
		params.maxTiles = zone.m_num_tiles_x * zone.m_num_tiles_z;
		int tiles_bits = log2(nextPow2(params.maxTiles));
		params.maxPolys = 1 << (22 - tiles_bits); // keep 10 bits for salt

		if (dtStatusFailed(zone.navmesh->init(&params))) {
			logError("Could not init Detour navmesh");
			return false;
		}

		for (u32 j = 0; j < zone.m_num_tiles_z; ++j) {
			for (u32 i = 0; i < zone.m_num_tiles_x; ++i) {
				if (!generateTile(zone, zone_entity, i, j, false)) {
					return false;
				}
			}
		}
		return true;
	}


	void addCrowdAgent(Agent& agent, RecastZone& zone) {
		ASSERT(zone.crowd);

		const Transform zone_tr = m_universe.getTransform(zone.entity);
		const Vec3 pos = Vec3(zone_tr.inverted().transform(m_universe.getPosition(agent.entity)));
		dtCrowdAgentParams params = {};
		params.radius = agent.radius;
		params.height = agent.height;
		params.maxAcceleration = 10.0f;
		params.maxSpeed = 10.0f;
		params.collisionQueryRange = params.radius * 12.0f;
		params.pathOptimizationRange = params.radius * 30.0f;
		params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_SEPARATION | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OPTIMIZE_VIS;
		agent.agent = zone.crowd->addAgent(&pos.x, &params);
		if (agent.agent < 0) {
			logError("Failed to create navigation actor");
		}
	}

	void createZone(EntityRef entity) {
		RecastZone zone;
		zone.zone.extents = Vec3(1);
		zone.zone.guid = randGUID();
		zone.zone.flags = NavmeshZone::AUTOLOAD;
		zone.entity = entity;
		m_zones.insert(entity, zone);
		m_universe.onComponentCreated(entity, NAVMESH_ZONE_TYPE, this);
	}

	void destroyZone(EntityRef entity) {
		auto iter = m_zones.find(entity);
		const RecastZone& zone = iter.value();
		if (zone.crowd) {
			for (Agent& agent : m_agents) {
				if (agent.zone == zone.entity) {
					zone.crowd->removeAgent(agent.agent);
					agent.agent = -1;
				}
			}
			dtFreeCrowd(zone.crowd);
		}

		m_zones.erase(iter);
		m_universe.onComponentDestroyed(entity, NAVMESH_ZONE_TYPE, this);
	}

	void assignZone(Agent& agent) {
		const DVec3 agent_pos = m_universe.getPosition(agent.entity);
		for (RecastZone& zone : m_zones) {
			const Transform inv_zone_tr = m_universe.getTransform(zone.entity).inverted();
			const Vec3 min = -zone.zone.extents;
			const Vec3 max = zone.zone.extents;
			const Vec3 pos = Vec3(inv_zone_tr.transform(agent_pos));
			if (pos.x > min.x && pos.y > min.y && pos.z > min.z 
				&& pos.x < max.x && pos.y < max.y && pos.z < max.z)
			{
				agent.zone = zone.entity;
				if (zone.crowd) addCrowdAgent(agent, zone);
				return;
			}
		}
	}

	void createAgent(EntityRef entity) {
		Agent agent;
		agent.zone = INVALID_ENTITY;
		agent.entity = entity;
		agent.radius = 0.5f;
		agent.height = 2.0f;
		agent.agent = -1;
		agent.flags = Agent::MOVE_ENTITY;
		agent.is_finished = true;
		m_agents.insert(entity, agent);
		assignZone(agent);
		m_universe.onComponentCreated(entity, NAVMESH_AGENT_TYPE, this);
	}

	void destroyAgent(EntityRef entity) {
		auto iter = m_agents.find(entity);
		const Agent& agent = iter.value();
		if (agent.zone.isValid()) {
			RecastZone& zone = m_zones[(EntityRef)agent.zone];
			if (zone.crowd && agent.agent >= 0) zone.crowd->removeAgent(agent.agent);
			m_agents.erase(iter);
		}
		m_universe.onComponentDestroyed(entity, NAVMESH_AGENT_TYPE, this);
	}

	i32 getVersion() const override { return (i32)NavigationSceneVersion::LATEST; }


	void serialize(OutputMemoryStream& serializer) override
	{
		int count = m_zones.size();
		serializer.write(count);
		for (auto iter = m_zones.begin(); iter.isValid(); ++iter) {
			serializer.write(iter.key());
			const NavmeshZone& zone = iter.value().zone;
			serializer.write(zone.extents);
			serializer.write(zone.guid);
			serializer.write(zone.flags);
		}

		count = m_agents.size();
		serializer.write(count);
		for (auto iter = m_agents.begin(), end = m_agents.end(); iter != end; ++iter) {
			serializer.write(iter.key());
			serializer.write(iter.value().radius);
			serializer.write(iter.value().height);
			serializer.write(iter.value().flags);
		}
	}


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
	{
		u32 count = 0;
		serializer.read(count);
		m_zones.reserve(count + m_zones.size());
		for (u32 i = 0; i < count; ++i) {
			RecastZone zone;
			EntityRef e;
			serializer.read(e);
			e = entity_map.get(e);
			serializer.read(zone.zone.extents);
			zone.entity = e;
			if (version > (i32)NavigationSceneVersion::ZONE_GUID) {
				serializer.read(zone.zone.guid);
				serializer.read(zone.zone.flags);
			}
			else {
				zone.zone.guid = randGUID();
				zone.zone.flags = NavmeshZone::AUTOLOAD;
			}
			m_zones.insert(e, zone);
			m_universe.onComponentCreated(e, NAVMESH_ZONE_TYPE, this);
			if (version > (i32)NavigationSceneVersion::ZONE_GUID && (zone.zone.flags & NavmeshZone::AUTOLOAD) != 0) {
				loadZone(e);
			}
		}

		serializer.read(count);
		m_agents.reserve(count + m_agents.size());
		for (u32 i = 0; i < count; ++i) {
			Agent agent;
			serializer.read(agent.entity);
			agent.entity = entity_map.get(agent.entity);
			serializer.read(agent.radius);
			serializer.read(agent.height);
			serializer.read(agent.flags);
			agent.is_finished = true;
			agent.agent = -1;
			m_agents.insert(agent.entity, agent);
			m_universe.onComponentCreated(agent.entity, NAVMESH_AGENT_TYPE, this);
		}
	}


	bool getAgentMoveEntity(EntityRef entity) override
	{
		return (m_agents[entity].flags & Agent::MOVE_ENTITY) != 0;
	}


	void setAgentMoveEntity(EntityRef entity, bool value) override
	{
		if (value)
			m_agents[entity].flags |= Agent::MOVE_ENTITY;
		else
			m_agents[entity].flags &= ~Agent::MOVE_ENTITY;
	}


	void setAgentRadius(EntityRef entity, float radius) override
	{
		m_agents[entity].radius = radius;
	}


	float getAgentRadius(EntityRef entity) override
	{
		return m_agents[entity].radius;
	}


	void setAgentHeight(EntityRef entity, float height) override
	{
		m_agents[entity].height = height;
	}


	float getAgentHeight(EntityRef entity) override
	{
		return m_agents[entity].height;
	}
	
	NavmeshZone& getZone(EntityRef entity) override {
		return m_zones[entity].zone;
	}

	bool isZoneAutoload(EntityRef entity) override {
		return m_zones[entity].zone.flags & NavmeshZone::AUTOLOAD;
	}
	
	void setZoneAutoload(EntityRef entity, bool value) {
		if (value) m_zones[entity].zone.flags |= NavmeshZone::AUTOLOAD;
		else m_zones[entity].zone.flags &= ~NavmeshZone::AUTOLOAD;
	}

	IPlugin& getPlugin() const override { return m_system; }
	Universe& getUniverse() override { return m_universe; }

	IAllocator& m_allocator;
	Universe& m_universe;
	IPlugin& m_system;
	Engine& m_engine;
	HashMap<EntityRef, RecastZone> m_zones;
	HashMap<EntityRef, Agent> m_agents;
	EntityPtr m_moving_agent = INVALID_ENTITY;
	bool m_is_game_running = false;
	
	Vec3 m_debug_tile_origin;
	rcConfig m_config;
	LuaScriptScene* m_script_scene;
	DelegateList<void(float)> m_on_update;
};


UniquePtr<NavigationScene> NavigationScene::create(Engine& engine, IPlugin& system, Universe& universe, IAllocator& allocator)
{
	return UniquePtr<NavigationSceneImpl>::create(allocator, engine, system, universe, allocator);
}


void NavigationScene::reflect() {
	LUMIX_SCENE(NavigationSceneImpl, "navigation")
		.LUMIX_FUNC(NavigationSceneImpl::setGeneratorParams)

		.LUMIX_CMP(Zone, "navmesh_zone", "Navigation / Zone")
			.icon(ICON_FA_STREET_VIEW)
			.LUMIX_FUNC_EX(loadZone, "load")
			.LUMIX_FUNC_EX(debugDrawContours, "drawContours")
			.LUMIX_FUNC_EX(debugDrawNavmesh, "drawNavmesh")
			.LUMIX_FUNC_EX(debugDrawCompactHeightfield, "drawCompactHeightfield")
			.LUMIX_FUNC_EX(debugDrawHeightfield, "drawHeightfield")
			.LUMIX_FUNC(NavigationSceneImpl::generateNavmesh)
			.var_prop<&NavigationScene::getZone, &NavmeshZone::extents>("Extents")
			.prop<&NavigationScene::isZoneAutoload, &NavigationScene::setZoneAutoload>("Autoload")
		.LUMIX_CMP(Agent, "navmesh_agent", "Navigation / Agent")
			.icon(ICON_FA_MAP_MARKED_ALT)
			.LUMIX_FUNC_EX(NavigationSceneImpl::setActorActive, "setActive")
			.LUMIX_FUNC_EX(NavigationSceneImpl::navigate, "navigate")
			.LUMIX_FUNC_EX(NavigationSceneImpl::cancelNavigation, "cancelNavigation")
			.LUMIX_FUNC_EX(NavigationSceneImpl::getAgentSpeed, "getSpeed")
			.LUMIX_FUNC_EX(NavigationSceneImpl::debugDrawPath, "drawPath")
			.LUMIX_PROP(AgentRadius, "Radius").minAttribute(0)
			.LUMIX_PROP(AgentHeight, "Height").minAttribute(0)
			.LUMIX_PROP(AgentMoveEntity, "Move entity");
}

} // namespace Lumix
