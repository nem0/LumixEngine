#pragma once

#include "engine/lumix.h"
#include "renderer/gpu/gpu.h"
#include "editor/world_editor.h"

using ImTextureID = void*;
struct ImDrawData;

namespace Lumix {

struct RenderInterface {
	virtual ~RenderInterface() {}

	virtual struct AABB getEntityAABB(Universe& universe, EntityRef entity, const DVec3& base) = 0;
	virtual ImTextureID createTexture(const char* name, const void* pixels, int w, int h) = 0;
	virtual void destroyTexture(ImTextureID handle) = 0;
	virtual ImTextureID loadTexture(const struct Path& path) = 0;
	virtual bool isValid(ImTextureID texture) = 0;
	virtual void unloadTexture(ImTextureID handle) = 0;
	virtual UniverseView::RayHit castRay(Universe& universe, const DVec3& origin, const Vec3& dir, EntityPtr ignored) = 0;
	virtual Path getModelInstancePath(Universe& universe, EntityRef entity) = 0;
	virtual bool saveTexture(Engine& engine, const char* path_cstr, const void* pixels, int w, int h, bool upper_left_origin) = 0;
};

}