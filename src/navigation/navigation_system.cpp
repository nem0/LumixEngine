#include "navigation_system.h"
#include "engine/array.h"
#include "engine/base_proxy_allocator.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/iallocator.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/lumix.h"
#include "engine/profiler.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "engine/serializer.h"
#include "engine/universe/universe.h"
#include "engine/vec.h"
#include "lua_script/lua_script_system.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"
#include <DetourAlloc.h>
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>
#include <RecastAlloc.h>
#include <cmath>


namespace Lumix
{


enum class AnimationSceneVersion : int
{
	USE_ROOT_MOTION,

	LATEST
};


static const ComponentType NAVMESH_AGENT_TYPE = PropertyRegister::getComponentType("navmesh_agent");
static const int CELLS_PER_TILE_SIDE = 256;
static const float CELL_SIZE = 0.3f;
static void registerLuaAPI(lua_State* L);


struct Agent
{
	enum Flags : u32
	{
		USE_ROOT_MOTION = 1 << 0
	};

	Entity entity;
	float radius;
	float height;
	int agent;
	bool is_finished;
	u32 flags = 0;
	Vec3 root_motion = {0, 0, 0};
	float speed = 0;
};


struct NavigationSystem LUMIX_FINAL : public IPlugin
{
	NavigationSystem(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
	{
		ASSERT(s_instance == nullptr);
		s_instance = this;
		dtAllocSetCustom(&detourAlloc, &detourFree);
		rcAllocSetCustom(&recastAlloc, &recastFree);
		registerLuaAPI(m_engine.getState());
		registerProperties();
		// register flags
		Material::getCustomFlag("no_navigation");
		Material::getCustomFlag("nonwalkable");
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


	void registerProperties();
	const char* getName() const override { return "navigation"; }
	void createScenes(Universe& universe) override;
	void destroyScene(IScene* scene) override;

	BaseProxyAllocator m_allocator;
	Engine& m_engine;
};


NavigationSystem* NavigationSystem::s_instance = nullptr;


struct NavigationSceneImpl LUMIX_FINAL : public NavigationScene
{
	NavigationSceneImpl(NavigationSystem& system, Universe& universe, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(universe)
		, m_system(system)
		, m_detail_mesh(nullptr)
		, m_polymesh(nullptr)
		, m_navquery(nullptr)
		, m_navmesh(nullptr)
		, m_debug_compact_heightfield(nullptr)
		, m_debug_heightfield(nullptr)
		, m_debug_contours(nullptr)
		, m_num_tiles_x(0)
		, m_num_tiles_z(0)
		, m_agents(m_allocator)
		, m_crowd(nullptr)
		, m_script_scene(nullptr)
	{
		setGeneratorParams(0.3f, 0.1f, 0.3f, 2.0f, 60.0f, 1.5f);
		m_universe.entityTransformed().bind<NavigationSceneImpl, &NavigationSceneImpl::onEntityMoved>(this);
		universe.registerComponentType(NAVMESH_AGENT_TYPE, this, &NavigationSceneImpl::serializeAgent, &NavigationSceneImpl::deserializeAgent);
	}


	~NavigationSceneImpl()
	{
		m_universe.entityTransformed().unbind<NavigationSceneImpl, &NavigationSceneImpl::onEntityMoved>(this);
		clearNavmesh();
	}


	void clear() override
	{
		m_agents.clear();
	}


	void onEntityMoved(Entity entity)
	{
		auto iter = m_agents.find(entity);
		if (!iter.isValid()) return;
		if (iter.value().agent < 0) return;
		const Agent& agent = iter.value();
		Vec3 pos = m_universe.getPosition(iter.key());
		const dtCrowdAgent* dt_agent = m_crowd->getAgent(agent.agent);
		if ((pos - *(Vec3*)dt_agent->npos).squaredLength() > 0.1f)
		{
			Vec3 target_pos = *(Vec3*)dt_agent->targetPos;
			float speed = dt_agent->params.maxSpeed;
			m_crowd->removeAgent(agent.agent);
			addCrowdAgent(iter.value());
			if (!agent.is_finished)
			{
				navigate(entity, target_pos, speed);
			}
		}
	}


	void clearNavmesh()
	{
		rcFreePolyMeshDetail(m_detail_mesh);
		rcFreePolyMesh(m_polymesh);
		dtFreeNavMeshQuery(m_navquery);
		dtFreeNavMesh(m_navmesh);
		dtFreeCrowd(m_crowd);
		rcFreeCompactHeightfield(m_debug_compact_heightfield);
		rcFreeHeightField(m_debug_heightfield);
		rcFreeContourSet(m_debug_contours);
		m_detail_mesh = nullptr;
		m_polymesh = nullptr;
		m_navquery = nullptr;
		m_navmesh = nullptr;
		m_crowd = nullptr;
		m_debug_compact_heightfield = nullptr;
		m_debug_heightfield = nullptr;
		m_debug_contours = nullptr;
	}


	void rasterizeGeometry(const AABB& aabb, rcContext& ctx, rcConfig& cfg, rcHeightfield& solid)
	{
		rasterizeMeshes(aabb, ctx, cfg, solid);
		rasterizeTerrains(aabb, ctx, cfg, solid);
	}


	AABB getTerrainSpaceAABB(const Vec3& terrain_pos, const Quat& terrain_rot, const AABB& aabb_world_space)
	{
		Matrix mtx = terrain_rot.toMatrix();
		mtx.setTranslation(terrain_pos);
		mtx.fastInverse();
		AABB ret = aabb_world_space;
		ret.transform(mtx);
		return ret;
	}


	void rasterizeTerrains(const AABB& aabb, rcContext& ctx, rcConfig& cfg, rcHeightfield& solid)
	{
		PROFILE_FUNCTION();
		const float walkable_threshold = cosf(Math::degreesToRadians(60));

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		ComponentHandle cmp = render_scene->getFirstTerrain();
		while (cmp != INVALID_COMPONENT)
		{
			Entity entity = render_scene->getTerrainEntity(cmp);
			Vec3 pos = m_universe.getPosition(entity);
			Quat rot = m_universe.getRotation(entity);
			Vec2 res = render_scene->getTerrainResolution(cmp);
			float scaleXZ = render_scene->getTerrainXZScale(cmp);
			AABB terrain_space_aabb = getTerrainSpaceAABB(pos, rot, aabb);
			int from_z = (int)Math::clamp(terrain_space_aabb.min.z / scaleXZ - 1, 0.0f, res.y - 1);
			int to_z = (int)Math::clamp(terrain_space_aabb.max.z / scaleXZ + 1, 0.0f, res.y - 1);
			int from_x = (int)Math::clamp(terrain_space_aabb.min.x / scaleXZ - 1, 0.0f, res.x - 1);
			int to_x = (int)Math::clamp(terrain_space_aabb.max.x / scaleXZ + 1, 0.0f, res.x - 1);
			for (int j = from_z; j < to_z; ++j)
			{
				for (int i = from_x; i < to_x; ++i)
				{
					float x = i * scaleXZ;
					float z = j * scaleXZ;
					float h0 = render_scene->getTerrainHeightAt(cmp, x, z);
					Vec3 p0 = pos + rot.rotate(Vec3(x, h0, z));

					x = (i + 1) * scaleXZ;
					z = j * scaleXZ;
					float h1 = render_scene->getTerrainHeightAt(cmp, x, z);
					Vec3 p1 = pos + rot.rotate(Vec3(x, h1, z));

					x = (i + 1) * scaleXZ;
					z = (j + 1) * scaleXZ;
					float h2 = render_scene->getTerrainHeightAt(cmp, x, z);
					Vec3 p2 = pos + rot.rotate(Vec3(x, h2, z));

					x = i * scaleXZ;
					z = (j + 1) * scaleXZ;
					float h3 = render_scene->getTerrainHeightAt(cmp, x, z);
					Vec3 p3 = pos + rot.rotate(Vec3(x, h3, z));

					Vec3 n = crossProduct(p1 - p0, p0 - p2).normalized();
					u8 area = n.y > walkable_threshold ? RC_WALKABLE_AREA : 0;
					rcRasterizeTriangle(&ctx, &p0.x, &p1.x, &p2.x, area, solid);

					n = crossProduct(p2 - p0, p0 - p3).normalized();
					area = n.y > walkable_threshold ? RC_WALKABLE_AREA : 0;
					rcRasterizeTriangle(&ctx, &p0.x, &p2.x, &p3.x, area, solid);
				}
			}

			cmp = render_scene->getNextTerrain(cmp);
		}
	}


	void rasterizeMeshes(const AABB& aabb, rcContext& ctx, rcConfig& cfg, rcHeightfield& solid)
	{
		PROFILE_FUNCTION();
		const float walkable_threshold = cosf(Math::degreesToRadians(45));

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		u32 no_navigation_flag = Material::getCustomFlag("no_navigation");
		u32 nonwalkable_flag = Material::getCustomFlag("nonwalkable");
		for (auto model_instance = render_scene->getFirstModelInstance(); model_instance != INVALID_COMPONENT;
			 model_instance = render_scene->getNextModelInstance(model_instance))
		{
			auto* model = render_scene->getModelInstanceModel(model_instance);
			if (!model) return;
			ASSERT(model->isReady());

			bool is16 = model->areIndices16();

			Entity entity = render_scene->getModelInstanceEntity(model_instance);
			Matrix mtx = m_universe.getMatrix(entity);
			AABB model_aabb = model->getAABB();
			model_aabb.transform(mtx);
			if (!model_aabb.overlaps(aabb)) continue;

			auto lod = model->getLODMeshIndices(0);
			for (int mesh_idx = lod.from; mesh_idx <= lod.to; ++mesh_idx)
			{
				auto& mesh = model->getMesh(mesh_idx);
				if (mesh.material->isCustomFlag(no_navigation_flag)) continue;
				bool is_walkable = !mesh.material->isCustomFlag(nonwalkable_flag);
				auto* vertices =
					&model->getVertices()[mesh.attribute_array_offset / model->getVertexDecl().getStride()];
				if (is16)
				{
					const u16* indices16 = model->getIndices16();
					for (int i = 0; i < mesh.indices_count; i += 3)
					{
						Vec3 a = mtx.transform(vertices[indices16[mesh.indices_offset + i]]);
						Vec3 b = mtx.transform(vertices[indices16[mesh.indices_offset + i + 1]]);
						Vec3 c = mtx.transform(vertices[indices16[mesh.indices_offset + i + 2]]);

						Vec3 n = crossProduct(a - b, a - c).normalized();
						u8 area = n.y > walkable_threshold && is_walkable ? RC_WALKABLE_AREA : 0;
						rcRasterizeTriangle(&ctx, &a.x, &b.x, &c.x, area, solid);
					}
				}
				else
				{
					const u32* indices32 = model->getIndices32();
					for (int i = 0; i < mesh.indices_count; i += 3)
					{
						Vec3 a = mtx.transform(vertices[indices32[mesh.indices_offset + i]]);
						Vec3 b = mtx.transform(vertices[indices32[mesh.indices_offset + i + 1]]);
						Vec3 c = mtx.transform(vertices[indices32[mesh.indices_offset + i + 2]]);

						Vec3 n = crossProduct(a - b, a - c).normalized();
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
		
		auto cmp = m_script_scene->getComponent(agent.entity);
		if (cmp == INVALID_COMPONENT) return;

		for (int i = 0, c = m_script_scene->getScriptCount(cmp); i < c; ++i)
		{
			auto* call = m_script_scene->beginFunctionCall(cmp, i, "onPathFinished");
			if (!call) continue;

			m_script_scene->endFunctionCall();
		}
	}


	bool isFinished(Entity entity) override
	{
		return m_agents[entity].is_finished;
	}


	float getAgentSpeed(Entity entity) override
	{
		return m_agents[entity].speed;
	}


	void setAgentRootMotion(Entity entity, const Vec3& root_motion) override
	{
		m_agents[entity].root_motion = root_motion;
	}


	Vec3 getAgentVelocity(Entity entity) override
	{
		Agent& agent = m_agents[entity];
		const dtCrowdAgent* dt_agent = m_crowd->getAgent(agent.agent);
		if (!dt_agent) return {0, 0, 0};
		return *(Vec3*)dt_agent->vel;
	}


	void update(float time_delta, bool paused) override
	{
		PROFILE_FUNCTION();
		if (!m_crowd) return;
		if (paused) return;
		m_crowd->update(time_delta, nullptr);
		
		for (auto& agent : m_agents)
		{
			if (agent.agent < 0) continue;
			const dtCrowdAgent* dt_agent = m_crowd->getAgent(agent.agent);
			if (dt_agent->paused) continue;

			Vec3 pos = m_universe.getPosition(agent.entity);
			Quat rot = m_universe.getRotation(agent.entity);
			Vec3 diff = *(Vec3*)dt_agent->npos - pos;
			if (agent.flags & Agent::USE_ROOT_MOTION)
			{
				*(Vec3*)dt_agent->npos = pos + rot.rotate(agent.root_motion);
				agent.root_motion.set(0, 0, 0);
				diff.y = 0;
				agent.speed = diff.length() / time_delta;
			}
		}
		
		m_crowd->doMove(time_delta);

		for (auto& agent : m_agents)
		{
			if (agent.agent < 0) continue;
			const dtCrowdAgent* dt_agent = m_crowd->getAgent(agent.agent);
			if (dt_agent->paused) continue;

			m_universe.setPosition(agent.entity, *(Vec3*)dt_agent->npos);

			if (dt_agent->ncorners == 0)
			{
				if (!agent.is_finished)
				{
					m_crowd->resetMoveTarget(agent.agent);
					agent.is_finished = true;
					onPathFinished(agent);
				}
			}
			else
			{
				agent.is_finished = false;
			}
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


	static void drawPoly(RenderScene* render_scene, const dtMeshTile& tile, const dtPoly& poly)
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
			render_scene->addDebugTriangle(v[0], v[1], v[2], 0xff00aaff, 0);
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
				render_scene->addDebugLine(*(Vec3*)tv[n], *(Vec3*)tv[m], 0xff0000ff, 0);
			}
		}
	}


	const dtCrowdAgent* getDetourAgent(Entity entity) override
	{
		if (!m_crowd) return nullptr;

		auto iter = m_agents.find(entity);
		if (iter == m_agents.end()) return nullptr;

		const Agent& agent = iter.value();
		if (agent.agent < 0) return nullptr;
		return m_crowd->getAgent(agent.agent);
	}


	void debugDrawPath(Entity entity) override
	{
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		if (!m_crowd) return;

		auto iter = m_agents.find(entity);
		if (iter == m_agents.end()) return;
		const Agent& agent = iter.value();
		if (agent.agent < 0) return;
		const dtCrowdAgent* dt_agent = m_crowd->getAgent(agent.agent);

		const dtPolyRef* path = dt_agent->corridor.getPath();
		const int npath = dt_agent->corridor.getPathCount();
		for (int j = 0; j < npath; ++j)
		{
			dtPolyRef ref = path[j];
			const dtMeshTile* tile = nullptr;
			const dtPoly* poly = nullptr;
			if (dtStatusFailed(m_navmesh->getTileAndPolyByRef(ref, &tile, &poly))) continue;

			drawPoly(render_scene, *tile, *poly);
		}

		Vec3 prev = *(Vec3*)dt_agent->npos;
		for (int i = 0; i < dt_agent->ncorners; ++i)
		{
			Vec3 tmp = *(Vec3*)&dt_agent->cornerVerts[i * 3];
			render_scene->addDebugLine(prev, tmp, 0xffff0000, 0);
			prev = tmp;
		}
		render_scene->addDebugCross(*(Vec3*)dt_agent->targetPos, 1.0f, 0xffffffff, 0);
	}


	bool hasDebugDrawData() const override
	{
		return m_debug_contours != nullptr;
	}


	void debugDrawContours() override
	{
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		if (!m_debug_contours) return;

		Vec3 orig = m_debug_tile_origin;
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


	bool isNavmeshReady() const override
	{
		return m_navmesh != nullptr;
	}


	void fileLoaded(FS::IFile& file, bool success)
	{
		if (!success) return;
		if (!initNavmesh()) return;

		file.read(&m_aabb, sizeof(m_aabb));
		file.read(&m_num_tiles_x, sizeof(m_num_tiles_x));
		file.read(&m_num_tiles_z, sizeof(m_num_tiles_z));
		dtNavMeshParams params;
		file.read(&params, sizeof(params));
		if (dtStatusFailed(m_navmesh->init(&params)))
		{
			g_log_error.log("Navigation") << "Could not init Detour navmesh";
			return;
		}
		for (int j = 0; j < m_num_tiles_z; ++j)
		{
			for (int i = 0; i < m_num_tiles_x; ++i)
			{
				int data_size;
				file.read(&data_size, sizeof(data_size));
				u8* data = (u8*)dtAlloc(data_size, DT_ALLOC_PERM);
				file.read(data, data_size);
				if (dtStatusFailed(m_navmesh->addTile(data, data_size, DT_TILE_FREE_DATA, 0, 0)))
				{
					file.close();
					dtFree(data);
					return;
				}
			}
		}

		file.close();
		if (!m_crowd) initCrowd();
	}


	bool load(const char* path) override
	{
		clearNavmesh();

		FS::ReadCallback cb;
		cb.bind<NavigationSceneImpl, &NavigationSceneImpl::fileLoaded>(this);
		FS::FileSystem& fs = m_system.m_engine.getFileSystem();
		return fs.openAsync(fs.getDefaultDevice(), Path(path), FS::Mode::OPEN_AND_READ, cb) != FS::FileSystem::INVALID_ASYNC;
	}

	
	bool save(const char* path) override
	{
		if (!m_navmesh) return false;

		FS::OsFile file;
		if (!file.open(path, FS::Mode::CREATE_AND_WRITE, m_allocator)) return false;

		file.write(&m_aabb, sizeof(m_aabb));
		file.write(&m_num_tiles_x, sizeof(m_num_tiles_x));
		file.write(&m_num_tiles_z, sizeof(m_num_tiles_z));
		auto params = m_navmesh->getParams();
		file.write(params, sizeof(*params));
		for (int j = 0; j < m_num_tiles_z; ++j)
		{
			for (int i = 0; i < m_num_tiles_x; ++i)
			{
				const auto* tile = m_navmesh->getTileAt(i, j, 0);
				file.write(&tile->dataSize, sizeof(tile->dataSize));
				file.write(tile->data, tile->dataSize);
			}
		}

		file.close();
		return true;
	}


	void debugDrawHeightfield() override
	{
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		if (!m_debug_heightfield) return;

		Vec3 orig = m_debug_tile_origin;
		int width = m_debug_heightfield->width;
		float cell_height = 0.1f;
		int rendered_cubes = 0;
		for(int z = 0; z < m_debug_heightfield->height; ++z)
		{
			for(int x = 0; x < width; ++x)
			{
				float fx = orig.x + x * CELL_SIZE;
				float fz = orig.z + z * CELL_SIZE;
				const rcSpan* span = m_debug_heightfield->spans[x + z * width];
				while(span)
				{
					Vec3 mins(fx, orig.y + span->smin * cell_height, fz);
					Vec3 maxs(fx + CELL_SIZE, orig.y + span->smax * cell_height, fz + CELL_SIZE);
					render_scene->addDebugCubeSolid(mins, maxs, 0xffff00ff, 0);
					render_scene->addDebugCube(mins, maxs, 0xff00aaff, 0);
					span = span->next;
				}
			}
		}
	}


	void debugDrawCompactHeightfield() override
	{
		static const int MAX_CUBES = 0xffFF;

		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;
		if (!m_debug_compact_heightfield) return;

		auto& chf = *m_debug_compact_heightfield;
		const float cs = chf.cs;
		const float ch = chf.ch;

		Vec3 orig = m_debug_tile_origin;

		int rendered_cubes = 0;
		for (int y = 0; y < chf.height; ++y)
		{
			for (int x = 0; x < chf.width; ++x)
			{
				float vx = orig.x + (float)x * cs;
				float vz = orig.z + (float)y * cs;

				const rcCompactCell& c = chf.cells[x + y * chf.width];

				for (u32 i = c.index, ni = c.index + c.count; i < ni; ++i)
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


	static void drawPolyBoundaries(RenderScene* render_scene,
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
							render_scene->addDebugLine(
								*(Vec3*)tv[n] + Vec3(0, 0.5f, 0), *(Vec3*)tv[m] + Vec3(0, 0.5f, 0), c, 0);
						}
					}
				}
			}
		}
	}


	static void drawTilePortal(RenderScene* render_scene, const dtMeshTile& tile)
	{
		const float padx = 0.04f;
		const float pady = tile.header->walkableClimb;

		for (int side = 0; side < 8; ++side)
		{
			unsigned short m = DT_EXT_LINK | (unsigned short)side;

			for (int i = 0; i < tile.header->polyCount; ++i)
			{
				dtPoly* poly = &tile.polys[i];

				const int nv = poly->vertCount;
				for (int j = 0; j < nv; ++j)
				{
					if (poly->neis[j] != m) continue;

					const float* va = &tile.verts[poly->verts[j] * 3];
					const float* vb = &tile.verts[poly->verts[(j + 1) % nv] * 3];

					if (side == 0 || side == 4)
					{
						unsigned int col = side == 0 ? 0xff0000aa : 0xff00aaaa;

						const float x = va[0] + ((side == 0) ? -padx : padx);

						render_scene->addDebugLine(Vec3(x, va[1] - pady, va[2]), Vec3(x, va[1] + pady, va[2]), col, 0);
						render_scene->addDebugLine(Vec3(x, va[1] + pady, va[2]), Vec3(x, vb[1] + pady, vb[2]), col, 0);
						render_scene->addDebugLine(Vec3(x, vb[1] + pady, vb[2]), Vec3(x, vb[1] - pady, vb[2]), col, 0);
						render_scene->addDebugLine(Vec3(x, vb[1] - pady, vb[2]), Vec3(x, va[1] - pady, va[2]), col, 0);

					}
					else if (side == 2 || side == 6)
					{
						unsigned int col = side == 2 ? 0xff00aa00 : 0xffaaaa00;

						const float z = va[2] + ((side == 2) ? -padx : padx);

						render_scene->addDebugLine(Vec3(va[0], va[1] - pady, z), Vec3(va[0], va[1] + pady, z), col, 0);
						render_scene->addDebugLine(Vec3(va[0], va[1] + pady, z), Vec3(vb[0], vb[1] + pady, z), col, 0);
						render_scene->addDebugLine(Vec3(vb[0], vb[1] + pady, z), Vec3(vb[0], vb[1] - pady, z), col, 0);
						render_scene->addDebugLine(Vec3(vb[0], vb[1] - pady, z), Vec3(va[0], va[1] - pady, z), col, 0);
					}

				}
			}
		}
	}


	void debugDrawNavmesh(const Vec3& pos, bool inner_boundaries, bool outer_boundaries, bool portals) override
	{
		if (pos.x > m_aabb.max.x || pos.x < m_aabb.min.x || pos.z > m_aabb.max.z || pos.z < m_aabb.min.z) return;

		int x = int((pos.x - m_aabb.min.x + (1 + m_config.borderSize) * m_config.cs) / (CELLS_PER_TILE_SIDE * CELL_SIZE));
		int z = int((pos.z - m_aabb.min.z + (1 + m_config.borderSize) * m_config.cs) / (CELLS_PER_TILE_SIDE * CELL_SIZE));
		const dtMeshTile* tile = m_navmesh->getTileAt(x, z, 0);
		auto render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		dtPolyRef base = m_navmesh->getPolyRefBase(tile);

		int tileNum = m_navmesh->decodePolyIdTile(base);

		for (int i = 0; i < tile->header->polyCount; ++i)
		{
			const dtPoly* p = &tile->polys[i];
			if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
			drawPoly(render_scene, *tile, *p);
		}

		if(outer_boundaries) drawPolyBoundaries(render_scene, *tile, 0xffff0000, false);
		if(inner_boundaries) drawPolyBoundaries(render_scene, *tile, 0xffff0000, true);

		if(portals) drawTilePortal(render_scene, *tile);
	}


	void stopGame() override
	{
		if (m_crowd)
		{
			for (Agent& agent : m_agents)
			{
				m_crowd->removeAgent(agent.agent);
				agent.agent = -1;
			}
			dtFreeCrowd(m_crowd);
			m_crowd = nullptr;
		}
	}


	void startGame() override
	{
		auto* scene = m_universe.getScene(crc32("lua_script"));
		m_script_scene = static_cast<LuaScriptScene*>(scene);
		
		if (m_navmesh && !m_crowd) initCrowd();
	}


	bool initCrowd()
	{
		ASSERT(!m_crowd);

		m_crowd = dtAllocCrowd();
		if (!m_crowd->init(1000, 4.0f, m_navmesh))
		{
			dtFreeCrowd(m_crowd);
			m_crowd = nullptr;
			return false;
		}
		for (auto iter = m_agents.begin(), end = m_agents.end(); iter != end; ++iter)
		{
			Agent& agent = iter.value();
			addCrowdAgent(agent);
		}

		return true;
	}


	void cancelNavigation(Entity entity)
	{
		auto iter = m_agents.find(entity);
		if (iter == m_agents.end()) return;

		Agent& agent = iter.value();
		if (agent.agent < 0) return;

		m_crowd->resetMoveTarget(agent.agent);
	}


	void setActorActive(Entity entity, bool active) override
	{
		if (!m_crowd) return;
		if (entity == INVALID_ENTITY) return;

		auto iter = m_agents.find(entity);
		if (iter == m_agents.end()) return;

		Agent& agent = iter.value();
		if (agent.agent < 0) return;

		dtCrowdAgent* dt_agent = m_crowd->getEditableAgent(agent.agent);
		if (dt_agent) dt_agent->paused = !active;
	}


	bool navigate(Entity entity, const Vec3& dest, float speed) override
	{
		if (!m_navquery) return false;
		if (!m_crowd) return false;
		if (entity == INVALID_ENTITY) return false;
		auto iter = m_agents.find(entity);
		if (iter == m_agents.end()) return false;
		Agent& agent = iter.value();
		if (agent.agent < 0) return false;
		dtPolyRef end_poly_ref;
		dtQueryFilter filter;
		static const float ext[] = { 1.0f, 20.0f, 1.0f };
		m_navquery->findNearestPoly(&dest.x, ext, &filter, &end_poly_ref, 0);
		dtCrowdAgentParams params = m_crowd->getAgent(agent.agent)->params;
		params.maxSpeed = speed;
		m_crowd->updateAgentParameters(agent.agent, &params);
		if (m_crowd->requestMoveTarget(agent.agent, end_poly_ref, &dest.x))
		{
			agent.is_finished = false;
		}
		else
		{
			g_log_warning.log("Navigation") << "requestMoveTarget failed";
			agent.is_finished = true;
		}
		return !agent.is_finished;
	}


	int getPolygonCount()
	{
		if (!m_polymesh) return 0;
		return m_polymesh->npolys;
	}


	void setGeneratorParams(float cell_size,
		float cell_height,
		float agent_radius,
		float agent_height,
		float walkable_angle,
		float max_climb)
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


	bool generateTileAt(const Vec3& pos, bool keep_data) override
	{
		int x = int((pos.x - m_aabb.min.x + (1 + m_config.borderSize) * m_config.cs) / (CELLS_PER_TILE_SIDE * CELL_SIZE));
		int z = int((pos.z - m_aabb.min.z + (1 + m_config.borderSize) * m_config.cs) / (CELLS_PER_TILE_SIDE * CELL_SIZE));
		return generateTile(x, z, keep_data);
	}


	bool generateTile(int x, int z, bool keep_data) override
	{
		PROFILE_FUNCTION();
		if (!m_navmesh) return false;
		m_navmesh->removeTile(m_navmesh->getTileRefAt(x, z, 0), 0, 0);

		rcContext ctx;

		Vec3 bmin(m_aabb.min.x + x * CELLS_PER_TILE_SIDE * CELL_SIZE - (1 + m_config.borderSize) * m_config.cs,
			m_aabb.min.y,
			m_aabb.min.z + z * CELLS_PER_TILE_SIDE * CELL_SIZE - (1 + m_config.borderSize) * m_config.cs);
		Vec3 bmax(bmin.x + CELLS_PER_TILE_SIDE * CELL_SIZE + (1 + m_config.borderSize) * m_config.cs,
			m_aabb.max.y,
			bmin.z + CELLS_PER_TILE_SIDE * CELL_SIZE + (1 + m_config.borderSize) * m_config.cs);
		if (keep_data) m_debug_tile_origin = bmin;
		rcVcopy(m_config.bmin, &bmin.x);
		rcVcopy(m_config.bmax, &bmax.x);
		rcHeightfield* solid = rcAllocHeightfield();
		m_debug_heightfield = keep_data ? solid : nullptr;
		if (!solid)
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Out of memory 'solid'.";
			return false;
		}
		if (!rcCreateHeightfield(
				&ctx, *solid, m_config.width, m_config.height, m_config.bmin, m_config.bmax, m_config.cs, m_config.ch))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not create solid heightfield.";
			return false;
		}
		rasterizeGeometry(AABB(bmin, bmax), ctx, m_config, *solid);

		rcFilterLowHangingWalkableObstacles(&ctx, m_config.walkableClimb, *solid);
		rcFilterLedgeSpans(&ctx, m_config.walkableHeight, m_config.walkableClimb, *solid);
		rcFilterWalkableLowHeightSpans(&ctx, m_config.walkableHeight, *solid);

		rcCompactHeightfield* chf = rcAllocCompactHeightfield();
		m_debug_compact_heightfield = keep_data ? chf : nullptr;
		if (!chf)
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Out of memory 'chf'.";
			return false;
		}

		if (!rcBuildCompactHeightfield(&ctx, m_config.walkableHeight, m_config.walkableClimb, *solid, *chf))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not build compact data.";
			return false;
		}

		if (!m_debug_heightfield) rcFreeHeightField(solid);

		if (!rcErodeWalkableArea(&ctx, m_config.walkableRadius, *chf))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not erode.";
			return false;
		}

		if (!rcBuildDistanceField(&ctx, *chf))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not build distance field.";
			return false;
		}

		if (!rcBuildRegions(&ctx, *chf, m_config.borderSize, m_config.minRegionArea, m_config.mergeRegionArea))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not build regions.";
			return false;
		}

		rcContourSet* cset = rcAllocContourSet();
		m_debug_contours = keep_data ? cset : nullptr;
		if (!cset)
		{
			ctx.log(RC_LOG_ERROR, "Could not generate navmesh: Out of memory 'cset'.");
			return false;
		}
		if (!rcBuildContours(&ctx, *chf, m_config.maxSimplificationError, m_config.maxEdgeLen, *cset))
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
		if (!rcBuildPolyMesh(&ctx, *cset, m_config.maxVertsPerPoly, *m_polymesh))
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

		if (!rcBuildPolyMeshDetail(
				&ctx, *m_polymesh, *chf, m_config.detailSampleDist, m_config.detailSampleMaxError, *m_detail_mesh))
		{
			g_log_error.log("Navigation") << "Could not generate navmesh: Could not build detail mesh.";
			return false;
		}

		if (!m_debug_compact_heightfield) rcFreeCompactHeightfield(chf);
		if (!m_debug_contours) rcFreeContourSet(cset);

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
		params.walkableHeight = m_config.walkableHeight * m_config.ch;
		params.walkableRadius = m_config.walkableRadius * m_config.cs;
		params.walkableClimb = m_config.walkableClimb * m_config.ch;
		params.tileX = x;
		params.tileY = z;
		rcVcopy(params.bmin, m_polymesh->bmin);
		rcVcopy(params.bmax, m_polymesh->bmax);
		params.cs = m_config.cs;
		params.ch = m_config.ch;
		params.buildBvTree = false;

		if (!dtCreateNavMeshData(&params, &nav_data, &nav_data_size))
		{
			g_log_error.log("Navigation") << "Could not build Detour navmesh.";
			return false;
		}
		if (dtStatusFailed(m_navmesh->addTile(nav_data, nav_data_size, DT_TILE_FREE_DATA, 0, nullptr)))
		{
			g_log_error.log("Navigation") << "Could not add Detour tile.";
			return false;
		}
		return true;
	}


	void computeAABB()
	{
		m_aabb.set(Vec3(0, 0, 0), Vec3(0, 0, 0));
		auto* render_scene = static_cast<RenderScene*>(m_universe.getScene(crc32("renderer")));
		if (!render_scene) return;

		for (auto model_instance = render_scene->getFirstModelInstance(); model_instance != INVALID_COMPONENT;
			model_instance = render_scene->getNextModelInstance(model_instance))
		{
			auto* model = render_scene->getModelInstanceModel(model_instance);
			if (!model) continue;
			ASSERT(model->isReady());

			AABB model_bb = model->getAABB();
			Matrix mtx = m_universe.getMatrix(render_scene->getModelInstanceEntity(model_instance));
			model_bb.transform(mtx);
			m_aabb.merge(model_bb);
		}

		ComponentHandle cmp = render_scene->getFirstTerrain();
		while (cmp != INVALID_COMPONENT)
		{
			AABB terrain_aabb = render_scene->getTerrainAABB(cmp);
			Matrix mtx = m_universe.getMatrix(render_scene->getTerrainEntity(cmp));
			terrain_aabb.transform(mtx);
			m_aabb.merge(terrain_aabb);

			cmp = render_scene->getNextTerrain(cmp);
		}
	}


	bool initNavmesh()
	{
		m_navmesh = dtAllocNavMesh();
		if (!m_navmesh)
		{
			g_log_error.log("Navigation") << "Could not create Detour navmesh";
			return false;
		}

		m_navquery = dtAllocNavMeshQuery();
		if (!m_navquery)
		{
			g_log_error.log("Navigation") << "Could not create Detour navmesh query";
			return false;
		}
		if (dtStatusFailed(m_navquery->init(m_navmesh, 2048)))
		{
			g_log_error.log("Navigation") << "Could not init Detour navmesh query";
			return false;
		}
		return true;
	}


	bool generateNavmesh() override
	{
		PROFILE_FUNCTION();
		clearNavmesh();

		if (!initNavmesh()) return false;

		computeAABB();
		dtNavMeshParams params;
		rcVcopy(params.orig, &m_aabb.min.x);
		params.tileWidth = float(CELLS_PER_TILE_SIDE * CELL_SIZE);
		params.tileHeight = float(CELLS_PER_TILE_SIDE * CELL_SIZE);
		int grid_width, grid_height;
		rcCalcGridSize(&m_aabb.min.x, &m_aabb.max.x, CELL_SIZE, &grid_width, &grid_height);
		m_num_tiles_x = (grid_width + CELLS_PER_TILE_SIDE - 1) / CELLS_PER_TILE_SIDE;
		m_num_tiles_z = (grid_height + CELLS_PER_TILE_SIDE - 1) / CELLS_PER_TILE_SIDE;
		params.maxTiles = m_num_tiles_x * m_num_tiles_z;
		int tiles_bits = Math::log2(Math::nextPow2(params.maxTiles));
		params.maxPolys = 1 << (22 - tiles_bits); // keep 10 bits for salt

		if (dtStatusFailed(m_navmesh->init(&params)))
		{
			g_log_error.log("Navigation") << "Could not init Detour navmesh";
			return false;
		}

		for (int j = 0; j < m_num_tiles_z; ++j)
		{
			for (int i = 0; i < m_num_tiles_x; ++i)
			{
				if (!generateTile(i, j, false))
				{
					return false;
				}
			}
		}
		return true;
	}


	void addCrowdAgent(Agent& agent)
	{
		ASSERT(m_crowd);

		Vec3 pos = m_universe.getPosition(agent.entity);
		dtCrowdAgentParams params = {};
		params.radius = agent.radius;
		params.height = agent.height;
		params.maxAcceleration = 10.0f;
		params.maxSpeed = 10.0f;
		params.collisionQueryRange = params.radius * 12.0f;
		params.pathOptimizationRange = params.radius * 30.0f;
		params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_SEPARATION | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OPTIMIZE_VIS;
		agent.agent = m_crowd->addAgent(&pos.x, &params);
		if (agent.agent < 0)
		{
			g_log_error.log("Navigation") << "Failed to create navigation actor";
		}
	}


	ComponentHandle createComponent(ComponentType type, Entity entity) override
	{
		if (type == NAVMESH_AGENT_TYPE)
		{
			Agent agent;
			agent.entity = entity;
			agent.radius = 0.5f;
			agent.height = 2.0f;
			agent.agent = -1;
			agent.flags = Agent::USE_ROOT_MOTION;
			agent.is_finished = true;
			if (m_crowd) addCrowdAgent(agent);
			m_agents.insert(entity, agent);
			ComponentHandle cmp = {entity.index};
			m_universe.addComponent(entity, type, this, cmp);
			return cmp;
		}
		return INVALID_COMPONENT;
	}


	void destroyComponent(ComponentHandle component, ComponentType type) override
	{
		if (type == NAVMESH_AGENT_TYPE)
		{
			Entity entity = { component.index };
			auto iter = m_agents.find(entity);
			const Agent& agent = iter.value();
			if (m_crowd && agent.agent >= 0) m_crowd->removeAgent(agent.agent);
			m_agents.erase(iter);
			m_universe.destroyComponent(entity, type, this, component);
		}
		else
		{
			ASSERT(false);
		}
	}


	int getVersion() const override { return (int)AnimationSceneVersion::LATEST; }


	void serializeAgent(ISerializer& serializer, ComponentHandle cmp)
	{
		Agent& agent = m_agents[{cmp.index}];
		serializer.write("radius", agent.radius);
		serializer.write("height", agent.height);
		serializer.write("use_root_motion", (agent.flags & Agent::USE_ROOT_MOTION) != 0);
	}


	void deserializeAgent(IDeserializer& serializer, Entity entity, int scene_version)
	{
		Agent agent;
		agent.entity = entity;
		serializer.read(&agent.radius);
		serializer.read(&agent.height);
		if (scene_version > (int)AnimationSceneVersion::USE_ROOT_MOTION)
		{
			agent.flags = 0;
			bool b;
			serializer.read(&b);
			if (b) agent.flags = Agent::USE_ROOT_MOTION;
		}
		agent.is_finished = true;
		agent.agent = -1;
		if (m_crowd) addCrowdAgent(agent);
		m_agents.insert(agent.entity, agent);
		ComponentHandle cmp = {agent.entity.index};
		m_universe.addComponent(agent.entity, NAVMESH_AGENT_TYPE, this, cmp);
	}


	void serialize(OutputBlob& serializer) override
	{
		int count = m_agents.size();
		serializer.write(count);
		for (auto iter = m_agents.begin(), end = m_agents.end(); iter != end; ++iter)
		{
			serializer.write(iter.key());
			serializer.write(iter.value().radius);
			serializer.write(iter.value().height);
			serializer.write(iter.value().flags);
		}
	}


	void deserialize(InputBlob& serializer) override
	{
		int count = 0;
		serializer.read(count);
		m_agents.rehash(count);
		for (int i = 0; i < count; ++i)
		{
			Agent agent;
			serializer.read(agent.entity);
			serializer.read(agent.radius);
			serializer.read(agent.height);
			serializer.read(agent.flags);
			agent.is_finished = true;
			agent.agent = -1;
			m_agents.insert(agent.entity, agent);
			ComponentHandle cmp = {agent.entity.index};
			m_universe.addComponent(agent.entity, NAVMESH_AGENT_TYPE, this, cmp);
		}
	}


	bool useAgentRootMotion(ComponentHandle cmp)
	{
		Entity entity = {cmp.index};
		return (m_agents[entity].flags & Agent::USE_ROOT_MOTION) != 0;
	}


	void setUseAgentRootMotion(ComponentHandle cmp, bool use_root_motion)
	{
		Entity entity = {cmp.index};
		if (use_root_motion)
			m_agents[entity].flags |= Agent::USE_ROOT_MOTION;
		else
			m_agents[entity].flags &= ~Agent::USE_ROOT_MOTION;
	}


	void setAgentRadius(ComponentHandle cmp, float radius)
	{
		Entity entity = {cmp.index};
		m_agents[entity].radius = radius;
	}


	float getAgentRadius(ComponentHandle cmp)
	{
		Entity entity = { cmp.index };
		return m_agents[entity].radius;
	}


	void setAgentHeight(ComponentHandle cmp, float height)
	{
		Entity entity = { cmp.index };
		m_agents[entity].height = height;
	}


	float getAgentHeight(ComponentHandle cmp)
	{
		Entity entity = {cmp.index};
		return m_agents[entity].height;
	}


	IPlugin& getPlugin() const override { return m_system; }
	ComponentHandle getComponent(Entity entity, ComponentType type) override
	{
		if (type == NAVMESH_AGENT_TYPE) return {entity.index};
		return INVALID_COMPONENT;
	}
	Universe& getUniverse() override { return m_universe; }

	IAllocator& m_allocator;
	Universe& m_universe;
	NavigationSystem& m_system;
	rcPolyMesh* m_polymesh;
	dtNavMesh* m_navmesh;
	dtNavMeshQuery* m_navquery;
	rcPolyMeshDetail* m_detail_mesh;
	HashMap<Entity, Agent> m_agents;
	rcCompactHeightfield* m_debug_compact_heightfield;
	rcHeightfield* m_debug_heightfield;
	rcContourSet* m_debug_contours;
	Vec3 m_debug_tile_origin;
	AABB m_aabb;
	rcConfig m_config;
	int m_num_tiles_x;
	int m_num_tiles_z;
	LuaScriptScene* m_script_scene;
	dtCrowd* m_crowd;
};


void NavigationSystem::registerProperties()
{
	auto& allocator = m_engine.getAllocator();
	PropertyRegister::add("navmesh_agent",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<NavigationSceneImpl>)(
			"radius", &NavigationSceneImpl::getAgentRadius, &NavigationSceneImpl::setAgentRadius, 0, 999.0f, 0.1f));
	PropertyRegister::add("navmesh_agent",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<NavigationSceneImpl>)(
			"height", &NavigationSceneImpl::getAgentHeight, &NavigationSceneImpl::setAgentHeight, 0, 999.0f, 0.1f));
	PropertyRegister::add("navmesh_agent",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<NavigationSceneImpl>)(
			"use root motion", &NavigationSceneImpl::useAgentRootMotion, &NavigationSceneImpl::setUseAgentRootMotion));
}


void NavigationSystem::createScenes(Universe& universe)
{
	auto* scene = LUMIX_NEW(m_allocator, NavigationSceneImpl)(*this, universe, m_allocator);
	universe.addScene(scene);
}


void NavigationSystem::destroyScene(IScene* scene)
{
	LUMIX_DELETE(m_allocator, scene);
}



LUMIX_PLUGIN_ENTRY(navigation)
{
	return LUMIX_NEW(engine.getAllocator(), NavigationSystem)(engine);
}


static void registerLuaAPI(lua_State* L)
{
	#define REGISTER_FUNCTION(name) \
		do {\
			auto f = &LuaWrapper::wrapMethod<NavigationSceneImpl, decltype(&NavigationSceneImpl::name), &NavigationSceneImpl::name>; \
			LuaWrapper::createSystemFunction(L, "Navigation", #name, f); \
		} while(false) \

	REGISTER_FUNCTION(generateNavmesh);
	REGISTER_FUNCTION(navigate);
	REGISTER_FUNCTION(setActorActive);
	REGISTER_FUNCTION(cancelNavigation);
	REGISTER_FUNCTION(debugDrawNavmesh);
	REGISTER_FUNCTION(debugDrawCompactHeightfield);
	REGISTER_FUNCTION(debugDrawHeightfield);
	REGISTER_FUNCTION(debugDrawPath);
	REGISTER_FUNCTION(getPolygonCount);
	REGISTER_FUNCTION(debugDrawContours);
	REGISTER_FUNCTION(generateTile);
	REGISTER_FUNCTION(save);
	REGISTER_FUNCTION(load);
	REGISTER_FUNCTION(setGeneratorParams);

	#undef REGISTER_FUNCTION
}


} // namespace Lumix
