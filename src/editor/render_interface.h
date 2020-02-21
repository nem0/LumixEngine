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
	virtual float castRay(ModelHandle model, const Vec3& origin, const Vec3& dir, const Pose* pose) = 0;
	virtual ModelHandle loadModel(Path& path) = 0;
	virtual void unloadModel(ModelHandle handle) = 0;
	virtual Vec3 getModelCenter(EntityRef entity) = 0;
	virtual bool saveTexture(Engine& engine, const char* path, const void* pixels, int w, int h, bool upper_left_origin) = 0;
	virtual ImTextureID createTexture(const char* name, const void* pixels, int w, int h) = 0;
	virtual void destroyTexture(ImTextureID handle) = 0;
	virtual ImTextureID loadTexture(const Path& path) = 0;
	virtual bool isValid(ImTextureID texture) = 0;
	virtual void unloadTexture(ImTextureID handle) = 0;
	virtual void addDebugCube(const DVec3& minimum, const DVec3& maximum, u32 color) = 0;
	virtual void addDebugCube(const DVec3& pos, const Vec3& dir, const Vec3& up, const Vec3& right, u32 color) = 0;
	virtual void addDebugCross(const DVec3& pos, float size, u32 color) = 0;
	virtual void addDebugLine(const DVec3& from, const DVec3& to, u32 color) = 0;
	virtual WorldEditor::RayHit castRay(const DVec3& origin, const Vec3& dir, EntityPtr ignored) = 0;
	virtual Path getModelInstancePath(EntityRef entity) = 0;
	virtual ImFont* addFont(const char* filename, int size) = 0;
	virtual DVec3 getClosestVertex(Universe* universe, EntityRef entity, const DVec3& pos) = 0;
	virtual void addText2D(float x, float y, u32 color, const char* text) = 0;
	virtual void addRect2D(const Vec2& a, const Vec2& b, u32 color) = 0;
	virtual void addRectFilled2D(const Vec2& a, const Vec2& b, u32 color) = 0;
	virtual void getRenderables(Array<EntityRef>& entities, const ShiftedFrustum& frustum) = 0;
	virtual ShiftedFrustum getFrustum(EntityRef camera, const Vec2& a, const Vec2& b) = 0;
};


}