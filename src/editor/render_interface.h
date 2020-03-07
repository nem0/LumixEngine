#pragma once


#include "engine/lumix.h"
#include "engine/math.h"
#include "editor/world_editor.h"

struct ImFont;
using ImTextureID = void*;


namespace Lumix
{


struct AABB;
struct Pose;
struct Path;
struct ShiftedFrustum;


struct RenderInterface
{
	using ModelHandle = int;

	struct Vertex
	{
		Vec3 position;
		u32 color;
	};

	virtual ~RenderInterface() {}

	virtual AABB getEntityAABB(Universe& universe, EntityRef entity, const DVec3& base) = 0;
	virtual float getCameraFOV(EntityRef entity) = 0;
	virtual bool isCameraOrtho(EntityRef entity) = 0;
	virtual float getCameraOrthoSize(EntityRef entity) = 0;
	virtual Vec2 getCameraScreenSize(EntityRef entity) = 0;
	virtual Vec3 getModelCenter(EntityRef entity) = 0;
	virtual bool saveTexture(Engine& engine, const char* path, const void* pixels, int w, int h, bool upper_left_origin) = 0;
	virtual ImTextureID createTexture(const char* name, const void* pixels, int w, int h) = 0;
	virtual void destroyTexture(ImTextureID handle) = 0;
	virtual ImTextureID loadTexture(const Path& path) = 0;
	virtual bool isValid(ImTextureID texture) = 0;
	virtual void unloadTexture(ImTextureID handle) = 0;
	virtual UniverseView::RayHit castRay(const DVec3& origin, const Vec3& dir, EntityPtr ignored) = 0;
	virtual Path getModelInstancePath(EntityRef entity) = 0;
	virtual ImFont* addFont(const char* filename, int size) = 0;
	virtual void addText2D(float x, float y, u32 color, const char* text) = 0;
	virtual ShiftedFrustum getFrustum(EntityRef camera, const Vec2& a, const Vec2& b) = 0;
	virtual void setUniverse(Universe* universe) = 0;
};


}