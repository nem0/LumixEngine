#pragma once

#include "engine/black.h.h"

#ifndef ImTextureID
	using ImTextureID = void*;
#endif
struct ImDrawData;

namespace black {

namespace gpu {
	enum class TextureFormat : u32;
}

struct RenderInterface {
	virtual ~RenderInterface() {}

	virtual struct AABB getEntityAABB(struct World& world, EntityRef entity, const struct DVec3& base) = 0;
	virtual ImTextureID createTexture(const char* name, const void* pixels, int w, int h, gpu::TextureFormat format) = 0;
	virtual void destroyTexture(ImTextureID handle) = 0;
	virtual ImTextureID loadTexture(const struct Path& path) = 0;
	virtual bool isValid(ImTextureID texture) = 0;
	virtual void unloadTexture(ImTextureID handle) = 0;
	virtual struct RayHit castRay(World& world, const struct Ray& ray, EntityPtr ignored) = 0;
	virtual Path getModelInstancePath(World& world, EntityRef entity) = 0;
	virtual bool saveTexture(struct Engine& engine, const char* path_cstr, const void* pixels, int w, int h, bool upper_left_origin) = 0;
};

}