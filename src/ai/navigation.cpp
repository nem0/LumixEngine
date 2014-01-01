#include "navigation.h"
#include <cstdio>
#include <Windows.h>
#include <gl/GL.h>
#include "core/vec3.h"
#include "core/array.h"
#include "detour/Recast.h"
#include "detour/RecastDebugDraw.h"
#include "detour/DebugDraw.h"
#include "detour/DetourNavMesh.h"
#include "detour/DetourNavMeshQuery.h"
#include "detour/DetourNavMeshBuilder.h"


namespace Lux
{


struct NavigationImpl
{
	struct Path
	{
		Entity entity;
		PODArray<Vec3> vertices;
		int vertex_count;
		int current_index;
		float speed;
	};

	rcPolyMesh* m_polymesh;
	rcPolyMeshDetail* m_detail_mesh;
	dtNavMesh* m_navmesh;
	dtNavMeshQuery* m_navquery;
	Array<Path> m_paths;
};



class CustomRcContext : public rcContext
{
	virtual void doLog(const rcLogCategory /*category*/, const char* msg, const int /*len*/)
	{
		printf(msg);
	}

};


class GLCheckerTexture
{
	unsigned int m_texId;
public:
	GLCheckerTexture() : m_texId(0)
	{
	}
	
	~GLCheckerTexture()
	{
		if (m_texId != 0)
			glDeleteTextures(1, &m_texId);
	}
	void bind()
	{
		if (m_texId == 0)
		{
			// Create checker pattern.
			const unsigned int col0 = duRGBA(215,215,215,255);
			const unsigned int col1 = duRGBA(255,255,255,255);
			static const int TSIZE = 64;
			unsigned int data[TSIZE*TSIZE];
			
			glGenTextures(1, &m_texId);
			glBindTexture(GL_TEXTURE_2D, m_texId);

			int level = 0;
			int size = TSIZE;
			while (size > 0)
			{
				for (int y = 0; y < size; ++y)
					for (int x = 0; x < size; ++x)
						data[x+y*size] = (x==0 || y==0) ? col0 : col1;
				glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size,size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
				size /= 2;
				level++;
			}
			
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, m_texId);
		}
	}
};
GLCheckerTexture g_tex;

class DebugDrawGL : public duDebugDraw
{
public:
	virtual void depthMask(bool state);
	virtual void texture(bool state);
	virtual void begin(duDebugDrawPrimitives prim, float size = 1.0f);
	virtual void vertex(const float* pos, unsigned int color);
	virtual void vertex(const float x, const float y, const float z, unsigned int color);
	virtual void vertex(const float* pos, unsigned int color, const float* uv);
	virtual void vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v);
	virtual void end();
};


void DebugDrawGL::depthMask(bool state)
{
	glDepthMask(state ? GL_TRUE : GL_FALSE);
}

void DebugDrawGL::texture(bool state)
{
	if (state)
	{
		glEnable(GL_TEXTURE_2D);
		g_tex.bind();
	}
	else
	{
		glDisable(GL_TEXTURE_2D);
	}
}

void DebugDrawGL::begin(duDebugDrawPrimitives prim, float size)
{
	switch (prim)
	{
		case DU_DRAW_POINTS:
			glPointSize(size);
			glBegin(GL_POINTS);
			break;
		case DU_DRAW_LINES:
			glLineWidth(size);
			glBegin(GL_LINES);
			break;
		case DU_DRAW_TRIS:
			glBegin(GL_TRIANGLES);
			break;
		case DU_DRAW_QUADS:
			glBegin(GL_QUADS);
			break;
	};
}

void DebugDrawGL::vertex(const float* pos, unsigned int color)
{
	glColor4ubv((GLubyte*)&color);
	glVertex3fv(pos);
}

void DebugDrawGL::vertex(const float x, const float y, const float z, unsigned int color)
{
	glColor4ubv((GLubyte*)&color);
	glVertex3f(x,y,z);
}

void DebugDrawGL::vertex(const float* pos, unsigned int color, const float* uv)
{
	glColor4ubv((GLubyte*)&color);
	glTexCoord2fv(uv);
	glVertex3fv(pos);
}

void DebugDrawGL::vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v)
{
	glColor4ubv((GLubyte*)&color);
	glTexCoord2f(u,v);
	glVertex3f(x,y,z);
}

void DebugDrawGL::end()
{
	glEnd();
	glLineWidth(1.0f);
	glPointSize(1.0f);
}


void Navigation::update(float dt)
{
	for(int i = 0; i < m_impl->m_paths.size(); ++i)
	{
		NavigationImpl::Path& path = m_impl->m_paths[i];
		int idx = path.current_index;
		const Vec3& pos = path.entity.getPosition();
		Vec3 v = path.vertices[idx] - pos;
		float len = v.length();		
		float speed = path.speed;
		if(len < speed * dt)
		{
			++path.current_index;
			path.entity.setPosition(path.vertices[idx]);
		}
		else
		{
			v *= speed * dt / len;
			path.entity.setPosition(pos + v);
		}
	}
	for(int i = m_impl->m_paths.size() - 1; i >= 0; --i)
	{
		if(m_impl->m_paths[i].current_index == m_impl->m_paths[i].vertex_count)
		{
			m_impl->m_paths.eraseFast(i);
		}
	}
}


void Navigation::destroy()
{
	if(m_impl)
	{
		rcFreePolyMeshDetail(m_impl->m_detail_mesh);
		rcFreePolyMesh(m_impl->m_polymesh);
		dtFreeNavMeshQuery(m_impl->m_navquery);
		dtFreeNavMesh(m_impl->m_navmesh);
		delete m_impl;
		m_impl = NULL;
	}
}


void Navigation::draw()
{
	DebugDrawGL dd;
	if(m_impl->m_polymesh)
	{
		duDebugDrawPolyMesh(&dd, *m_impl->m_polymesh);
	}
	/*glBegin(GL_LINES);
		glColor3f(1, 0, 0);
		for(int i = 0; i < fcount - 1; ++i)
		{
			glVertex3fv(fpath + i * 3 );
			glVertex3fv(fpath + i * 3 + 3);
		}
	glEnd();*/
}


Navigation::Navigation()
{
	m_impl = new NavigationImpl;
	m_impl->m_polymesh = 0;
	m_impl->m_detail_mesh = 0;
	m_impl->m_navmesh = 0;
	m_impl->m_navquery = 0;
}


Navigation::~Navigation()
{
	ASSERT(m_impl == NULL);
}


void Navigation::navigate(Entity e, const Vec3& dest, float speed)
{
	if(e.isValid())
	{
		NavigationImpl::Path& path = m_impl->m_paths.pushEmpty();
		path.speed = speed;
		path.vertices.resize(128);
		path.entity = e;
		path.current_index = 0;

		dtPolyRef start_poly_ref;
		dtPolyRef end_poly_ref;
		Vec3 start_pos(e.getPosition());
	
		dtQueryFilter filter;
		dtPolyRef path_poly_ref[256];
		int path_count;
		const float ext[] = {0.1f, 2, 0.1f};
		m_impl->m_navquery->findNearestPoly(&start_pos.x, ext, &filter, &start_poly_ref, 0);
		m_impl->m_navquery->findNearestPoly(&dest.x, ext, &filter, &end_poly_ref, 0);
		m_impl->m_navquery->findPath(start_poly_ref, end_poly_ref, &start_pos.x, &dest.x, &filter, path_poly_ref, &path_count, 256);
		dtPolyRef frefs[256];
		unsigned char fflags[256];
		m_impl->m_navquery->findStraightPath(&start_pos.x, &dest.x, path_poly_ref, path_count, &path.vertices[0].x, fflags, frefs, &path.vertex_count, 256);
	}
}


bool Navigation::load(const char path[])
{
	ASSERT(false);
	return false;
	/// TODO
/*	if (!m_geom || !m_geom->getMesh())
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Input mesh is not specified.");
		return false;
	}
	
	cleanup();*/
	
	FILE* fp;
	fopen_s(&fp, path, "rb");
	if(!fp)
	{
		return false;
	}
	PODArray<Vec3> v_verts;
	int nverts, ntris;
	PODArray<unsigned int> v_tris;

	fread(&nverts, sizeof(nverts), 1, fp);
	v_verts.resize(nverts);
	fread(&v_verts[0], sizeof(Vec3), nverts, fp);
	fread(&ntris, sizeof(ntris), 1, fp);
	v_tris.resize(ntris);
	fread(&v_tris[0], sizeof(unsigned int), ntris, fp);
	ntris /= 3;
	/*physx::PxTriangleMeshDesc meshDesc;
	meshDesc.points.count = num_verts;
	meshDesc.points.stride = sizeof(physx::PxVec3);
	meshDesc.points.data = &verts[0];

	meshDesc.triangles.count = num_indices / 3;
	meshDesc.triangles.stride = 3*sizeof(physx::PxU32);
	meshDesc.triangles.data	= &tris[0];*/

	fclose(fp);
/****/
	/*const float* bmin = m_geom->getMeshBoundsMin();
	const float* bmax = m_geom->getMeshBoundsMax();*/
	const float* verts = &v_verts[0].x;//m_geom->getMesh()->getVerts();
	//const int nverts = m_geom->getMesh()->getVertCount();
	const int* tris = (int*)&v_tris[0];//m_geom->getMesh()->getTris();
	//const int ntris = m_geom->getMesh()->getTriCount();
	const float bmin[] = { -30, -30, -30};
	const float bmax[] = { 30, 30, 30};
	//
	// Step 1. Initialize build config.
	//
	rcConfig cfg;
	CustomRcContext ctx;
	// Init build configuration from GUI
	memset(&cfg, 0, sizeof(cfg));
	cfg.cs = 0.3f; //m_cellSize;
	cfg.ch = 0.1f; //m_cellHeight;
	cfg.walkableSlopeAngle = 45.0f; //m_agentMaxSlope;
	cfg.walkableHeight = (int)ceilf(2.0f/*m_agentHeight*/ / cfg.ch);
	cfg.walkableClimb = (int)floorf(0.9f/*m_agentMaxClimb*/ / cfg.ch);
	cfg.walkableRadius = (int)ceilf(0.6f/*m_agentRadius*/ / cfg.cs);
	cfg.maxEdgeLen = (int)(12.0f/*m_edgeMaxLen*/ / 0.3f/*m_cellSize*/);
	cfg.maxSimplificationError = 1.3f; //m_edgeMaxError;
	cfg.minRegionArea = (int)rcSqr(8/*m_regionMinSize*/);		// Note: area = size*size
	cfg.mergeRegionArea = (int)rcSqr(20/*m_regionMergeSize*/);	// Note: area = size*size
	cfg.maxVertsPerPoly = (int)6;//m_vertsPerPoly;
	cfg.detailSampleDist = 6.0f/*m_detailSampleDist*/ < 0.9f ? 0 : 0.3f/*m_cellSize*/ * 6.0f; //m_detailSampleDist;
	cfg.detailSampleMaxError = 0.1f * 1.0f;//m_cellHeight * m_detailSampleMaxError;
	
	// Set the area where the navigation will be build.
	// Here the bounds of the input mesh are used, but the
	// area could be specified by an user defined box, etc.
	rcVcopy(cfg.bmin, bmin);
	rcVcopy(cfg.bmax, bmax);
	rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

	// Reset build times gathering.
	ctx.resetTimers();

	// Start the build process.	
	ctx.startTimer(RC_TIMER_TOTAL);
	
	ctx.log(RC_LOG_PROGRESS, "Building navigation:");
	ctx.log(RC_LOG_PROGRESS, " - %d x %d cells", cfg.width, cfg.height);
	ctx.log(RC_LOG_PROGRESS, " - %.1fK verts, %.1fK tris", nverts/1000.0f, ntris/1000.0f);
	
	//
	// Step 2. Rasterize input polygon soup.
	//
	
	// Allocate voxel heightfield where we rasterize our input data to.
	rcHeightfield* solid = rcAllocHeightfield();
	if (!solid)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'solid'.");
		return false;
	}
	if (!rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not create solid heightfield.");
		return false;
	}
	
	// Allocate array that can hold triangle area types.
	// If you have multiple meshes you need to process, allocate
	// and array which can hold the max number of triangles you need to process.
	unsigned char* triareas = new unsigned char[ntris];
	if (!triareas)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'triareas' (%d).", ntris);
		return false;
	}
	
	// Find triangles which are walkable based on their slope and rasterize them.
	// If your input data is multiple meshes, you can transform them here, calculate
	// the are type for each of the meshes and rasterize them.
	memset(triareas, 0, ntris*sizeof(unsigned char));
	rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, nverts, tris, ntris, triareas);
	rcRasterizeTriangles(&ctx, verts, nverts, tris, triareas, ntris, *solid, cfg.walkableClimb);

	delete [] triareas;
	triareas = 0;
	
	//
	// Step 3. Filter walkables surfaces.
	//
	
	// Once all geoemtry is rasterized, we do initial pass of filtering to
	// remove unwanted overhangs caused by the conservative rasterization
	// as well as filter spans where the character cannot possibly stand.
	rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
	rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
	rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);


	//
	// Step 4. Partition walkable surface to simple regions.
	//

	// Compact the heightfield so that it is faster to handle from now on.
	// This will result more cache coherent data as well as the neighbours
	// between walkable cells will be calculated.
	rcCompactHeightfield* chf = rcAllocCompactHeightfield();
	if (!chf)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'chf'.");
		return false;
	}
	if (!rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build compact data.");
		return false;
	}
	
	rcFreeHeightField(solid);
	solid = 0;
		
	// Erode the walkable area by agent radius.
	if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not erode.");
		return false;
	}

	
	// (Optional) Mark areas.
	/*const ConvexVolume* vols = m_geom->getConvexVolumes();
	for (int i  = 0; i < m_geom->getConvexVolumeCount(); ++i)
		rcMarkConvexPolyArea(ctx, vols[i].verts, vols[i].nverts, vols[i].hmin, vols[i].hmax, (unsigned char)vols[i].area, *chf);
	*/
	/*if (m_monotonePartitioning)
	{
		// Partition the walkable surface into simple regions without holes.
		// Monotone partitioning does not need distancefield.
		if (!rcBuildRegionsMonotone(ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
		{
			ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build regions.");
			return false;
		}
	}
	else*/
	{
		// Prepare for region partitioning, by calculating distance field along the walkable surface.
		if (!rcBuildDistanceField(&ctx, *chf))
		{
			ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build distance field.");
			return false;
		}

		// Partition the walkable surface into simple regions without holes.
		if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
		{
			ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build regions.");
			return false;
		}
	}

	//
	// Step 5. Trace and simplify region contours.
	//
	
	// Create contours.
	rcContourSet* cset = rcAllocContourSet();
	if (!cset)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'cset'.");
		return false;
	}
	if (!rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not create contours.");
		return false;
	}
	
	//
	// Step 6. Build polygons mesh from contours.
	//
	
	// Build polygon navmesh from the contours.
	m_impl->m_polymesh = rcAllocPolyMesh();
	if (!m_impl->m_polymesh)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'm_polymesh'.");
		return false;
	}
	if (!rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *m_impl->m_polymesh))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not triangulate contours.");
		return false;
	}
	
	//
	// Step 7. Create detail mesh which allows to access approximate height on each polygon.
	//
	
	m_impl->m_detail_mesh = rcAllocPolyMeshDetail();
	if (!m_impl->m_detail_mesh)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Out of memory 'pmdtl'.");
		return false;
	}

	if (!rcBuildPolyMeshDetail(&ctx, *m_impl->m_polymesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *m_impl->m_detail_mesh))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build detail mesh.");
		return false;
	}

	rcFreeCompactHeightfield(chf);
	chf = 0;
	rcFreeContourSet(cset);
	cset = 0;

	// At this point the navigation mesh data is ready, you can access it from m_polymesh.
	// See duDebugDrawPolyMesh or dtCreateNavMeshData as examples how to access the data.
	
	//
	// (Optional) Step 8. Create Detour data from Recast poly mesh.
	//
	
	// The GUI may allow more max points per polygon than Detour can handle.
	// Only build the detour navmesh if we do not exceed the limit.
	/*if (cfg.maxVertsPerPoly <= DT_VERTS_PER_POLYGON)
	{*/
		unsigned char* navData = 0;
		int navDataSize = 0;
		
		// Update poly flags from areas.
		for (int i = 0; i < m_impl->m_polymesh->npolys; ++i)
		{
			if (m_impl->m_polymesh->areas[i] == RC_WALKABLE_AREA)
				m_impl->m_polymesh->flags[i] = 1;
			else				
				m_impl->m_polymesh->flags[i] = 0;
		}


		dtNavMeshCreateParams params;
		memset(&params, 0, sizeof(params));
		params.verts = m_impl->m_polymesh->verts;
		params.vertCount = m_impl->m_polymesh->nverts;
		params.polys = m_impl->m_polymesh->polys;
		params.polyAreas = m_impl->m_polymesh->areas;
		params.polyFlags = m_impl->m_polymesh->flags;
		params.polyCount = m_impl->m_polymesh->npolys;
		params.nvp = m_impl->m_polymesh->nvp;
		params.detailMeshes = m_impl->m_detail_mesh->meshes;
		params.detailVerts = m_impl->m_detail_mesh->verts;
		params.detailVertsCount = m_impl->m_detail_mesh->nverts;
		params.detailTris = m_impl->m_detail_mesh->tris;
		params.detailTriCount = m_impl->m_detail_mesh->ntris;
		/*params.offMeshConVerts = m_geom->getOffMeshConnectionVerts();
		params.offMeshConRad = m_geom->getOffMeshConnectionRads();
		params.offMeshConDir = m_geom->getOffMeshConnectionDirs();
		params.offMeshConAreas = m_geom->getOffMeshConnectionAreas();
		params.offMeshConFlags = m_geom->getOffMeshConnectionFlags();
		params.offMeshConUserID = m_geom->getOffMeshConnectionId();
		params.offMeshConCount = m_geom->getOffMeshConnectionCount();*/
		params.walkableHeight = (float)cfg.walkableHeight;
		params.walkableRadius = (float)cfg.walkableRadius;
		params.walkableClimb = (float)cfg.walkableClimb;
		rcVcopy(params.bmin, m_impl->m_polymesh->bmin);
		rcVcopy(params.bmax, m_impl->m_polymesh->bmax);
		params.cs = cfg.cs;
		params.ch = cfg.ch;
		params.buildBvTree = true;
		
		if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
		{
			ctx.log(RC_LOG_ERROR, "Could not build Detour navmesh.");
			return false;
		}
		
		m_impl->m_navmesh = dtAllocNavMesh();
		if (!m_impl->m_navmesh)
		{
			dtFree(navData);
			ctx.log(RC_LOG_ERROR, "Could not create Detour navmesh");
			return false;
		}
		
		dtStatus status;
		
		status = m_impl->m_navmesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
		if (dtStatusFailed(status))
		{
			dtFree(navData);
			ctx.log(RC_LOG_ERROR, "Could not init Detour navmesh");
			return false;
		}
		
		m_impl->m_navquery = dtAllocNavMeshQuery();
		status = m_impl->m_navquery->init(m_impl->m_navmesh, 2048);
		if (dtStatusFailed(status))
		{
			ctx.log(RC_LOG_ERROR, "Could not init Detour navmesh query");
			return false;
		}
	/*}
	ctx.stopTimer(RC_TIMER_TOTAL);
	
	// Show performance stats.
	duLogBuildTimes(*ctx, ctx.getAccumulatedTime(RC_TIMER_TOTAL));
	ctx.log(RC_LOG_PROGRESS, ">> Polymesh: %d vertices  %d polygons", m_polymesh->nverts, m_polymesh->npolys);
	
	m_totalBuildTimeMs = ctx.getAccumulatedTime(RC_TIMER_TOTAL)/1000.0f;
	
	if (m_tool)
		m_tool->init(this);
	initToolStates(this);*/
	return true;
}



extern "C" IPlugin* createPlugin()
{
	return new Navigation();
}



}