#include "ui_resource.h"
#include "core/log.h"

namespace Lumix::ui {

const ResourceType DocumentResource::TYPE("ui");

DocumentResource::DocumentResource(const Path& path, ResourceManager& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_content(allocator)
{}

void DocumentResource::unload() {
	m_content = "";
}

bool DocumentResource::load(Span<const u8> mem) {
	m_content = StringView((const char*)mem.m_begin, mem.length());
	return true;
}

Resource* DocumentResourceManager::createResource(const Path& path) {
	return LUMIX_NEW(m_allocator, DocumentResource)(path, *this, m_allocator);
}

void DocumentResourceManager::destroyResource(Resource& resource) {
	LUMIX_DELETE(m_allocator, static_cast<DocumentResource*>(&resource));
}

} // namespace Lumix