#include "font_manager.h"


namespace Lumix
{


Resource* FontManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, FontResource)(path, *this, m_allocator);
}


void FontManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<FontResource*>(&resource));
}


} // namespace Lumix