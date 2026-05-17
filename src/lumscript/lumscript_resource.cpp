#include "lumscript/lumscript_resource.h"
#include "core/log.h"

namespace Lumix {

const ResourceType LumScriptResource::TYPE("lumscript");

LumScriptResource::LumScriptResource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_source_code(allocator)
{}

LumScriptResource::~LumScriptResource() = default;

void LumScriptResource::unload() {
	m_source_code = "";
}

bool LumScriptResource::load(Span<const u8> mem) {
	// Load the .lum file as UTF-8 text
	m_source_code = "";
	m_source_code.append(StringView((const char*)mem.begin(), (const char*)mem.end()));
	return true;
}

Resource* LumScriptResourceManager::createResource(const Path& path) {
	return LUMIX_NEW(m_allocator, LumScriptResource)(path, *this, m_allocator);
}

void LumScriptResourceManager::destroyResource(Resource& resource) {
	LUMIX_DELETE(m_allocator, static_cast<LumScriptResource*>(&resource));
}

} // namespace Lumix
