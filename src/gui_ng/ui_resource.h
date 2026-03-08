#pragma once

#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "core/span.h"
#include "core/string.h"

namespace Lumix::ui {

struct DocumentResource final : Resource {
	DocumentResource(const Path& path, ResourceManager& manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(Span<const u8> mem) override;

	const String& getContent() const { return m_content; }

	static const ResourceType TYPE;

private:
	String m_content;
};

struct DocumentResourceManager final : ResourceManager {
	explicit DocumentResourceManager(IAllocator& allocator) : ResourceManager(allocator) {}

	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;
};

} // namespace Lumix