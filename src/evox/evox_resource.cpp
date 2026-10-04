#include "evox/evox_resource.h"
#include "core/log.h"

namespace Lumix {

const ResourceType EvoxResource::TYPE("evox");

EvoxResource::EvoxResource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_bytecode(allocator)
{}

EvoxResource::~EvoxResource() = default;

void EvoxResource::unload() {
	m_bytecode.clear();
}

bool EvoxResource::load(Span<const u8> mem) {
	m_bytecode.clear();
	m_bytecode.write(mem.begin(), mem.length());
	return true;
}

Resource* EvoxResourceManager::createResource(const Path& path) {
	return LUMIX_NEW(m_allocator, EvoxResource)(path, *this, m_allocator);
}

void EvoxResourceManager::destroyResource(Resource& resource) {
	LUMIX_DELETE(m_allocator, static_cast<EvoxResource*>(&resource));
}

} // namespace Lumix
