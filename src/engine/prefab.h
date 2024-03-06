#pragma once

#include "core/hash.h"
#include "engine/resource.h"
#include "core/stream.h"

namespace Lumix {

enum class PrefabVersion : u32 {
	FIRST,
	WITH_HIERARCHY,

	LAST
};

struct LUMIX_ENGINE_API PrefabResource final : Resource {
	PrefabResource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	ResourceType getType() const override;
	void unload() override;
	bool load(Span<const u8> mem) override;

	OutputMemoryStream data;
	StableHash content_hash;
	static const ResourceType TYPE;
};

} // namespace Lumix