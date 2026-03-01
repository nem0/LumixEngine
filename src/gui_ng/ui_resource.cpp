#include "ui_resource.h"
#include "core/log.h"

namespace Lumix {

const ResourceType UIDocument::TYPE("ui");

UIDocument::UIDocument(const Path& path, ResourceManager& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_blob()
{}

void UIDocument::unload() {
	m_blob = Span<const u8>();
}

bool UIDocument::load(Span<const u8> mem) {
	m_blob = mem;
	return true;
}

Resource* UIDocumentManager::createResource(const Path& path) {
	return LUMIX_NEW(m_allocator, UIDocument)(path, *this, m_allocator);
}

void UIDocumentManager::destroyResource(Resource& resource) {
	LUMIX_DELETE(m_allocator, static_cast<UIDocument*>(&resource));
}

} // namespace Lumix