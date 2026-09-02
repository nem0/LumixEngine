#pragma once

#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "core/string.h"

namespace Lumix {

struct EvoxResource final : Resource {
public:
	EvoxResource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	virtual ~EvoxResource();

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(Span<const u8> mem) override;
	StringView getSourceCode() const { return m_source_code; }

	static const ResourceType TYPE;

private:
	IAllocator& m_allocator;
	String m_source_code;
};

struct EvoxResourceManager final : ResourceManager {
	explicit EvoxResourceManager(IAllocator& allocator) : ResourceManager(allocator) {}

	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;
};

} // namespace Lumix
