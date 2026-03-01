#pragma once

#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "core/span.h"

namespace Lumix {

struct UIDocument final : Resource {
	UIDocument(const Path& path, ResourceManager& manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(Span<const u8> mem) override;

	Span<const u8> getBlob() const { return m_blob; }

	static const ResourceType TYPE;

private:
	Span<const u8> m_blob;
};

struct UIDocumentManager final : ResourceManager {
	explicit UIDocumentManager(IAllocator& allocator) : ResourceManager(allocator) {}

	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;
};

} // namespace Lumix