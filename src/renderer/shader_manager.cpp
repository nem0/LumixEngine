#include "engine/lumix.h"
#include "renderer/shader_manager.h"

#include "engine/resource.h"
#include "renderer/shader.h"

namespace Lumix
{


ShaderManager::ShaderManager(Renderer& renderer, IAllocator& allocator)
	: ResourceManager(allocator)
	, m_allocator(allocator)
	, m_renderer(renderer)
{
}


ShaderManager::~ShaderManager()
{
}


Resource* ShaderManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, Shader)(path, *this, m_renderer, m_allocator);
}


void ShaderManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<Shader*>(&resource));
}


} // namespace Lumix
