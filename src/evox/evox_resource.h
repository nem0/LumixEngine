#pragma once

#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "core/stream.h"

namespace Lumix {

// Roots of the project's Evox bytecode. `render.evox` is compiled as a second root of `main.evox`, see EvoxRender.
static constexpr const char* EVOX_MAIN_PATH = "main.evox";
static constexpr const char* EVOX_RENDER_PATH = "render.evox";

// Holds the serialized bytecode produced by the asset compiler. The runtime
// never sees evox source code, it instantiates ex_bytecode from this image.
struct EvoxResource final : Resource {
public:
	EvoxResource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	virtual ~EvoxResource();

	ResourceType getType() const override { return TYPE; }

	void unload() override;
	bool load(Span<const u8> mem) override;
	Span<const u8> getBytecode() const { return Span(m_bytecode.data(), (u32)m_bytecode.size()); }

	static const ResourceType TYPE;

private:
	OutputMemoryStream m_bytecode;
};

struct EvoxResourceManager final : ResourceManager {
	explicit EvoxResourceManager(IAllocator& allocator) : ResourceManager(allocator) {}

	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;
};

} // namespace Lumix
