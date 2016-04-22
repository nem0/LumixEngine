#include "engine/lumix.h"
#include "renderer/shader_manager.h"

#include "engine/core/resource.h"
#include "renderer/shader.h"

namespace Lumix
{


ShaderManager::ShaderManager(Renderer& renderer, IAllocator& allocator)
	: ResourceManagerBase(allocator)
	, m_allocator(allocator)
	, m_renderer(renderer)
{
	m_buffer = nullptr;
	m_buffer_size = -1;
}


ShaderManager::~ShaderManager()
{
	LUMIX_DELETE(m_allocator, m_buffer);
}


Resource* ShaderManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, Shader)(path, getOwner(), m_allocator);
}


void ShaderManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<Shader*>(&resource));
}


uint8* ShaderManager::getBuffer(int32 size)
{
	if (m_buffer_size < size)
	{
		LUMIX_DELETE(m_allocator, m_buffer);
		m_buffer = nullptr;
		m_buffer_size = -1;
	}
	if (m_buffer == nullptr)
	{
		m_buffer = (uint8*)m_allocator.allocate(sizeof(uint8) * size);
		m_buffer_size = size;
	}
	return m_buffer;
}


ShaderBinaryManager::ShaderBinaryManager(Renderer& renderer, IAllocator& allocator)
	: ResourceManagerBase(allocator)
	, m_allocator(allocator)
{
}


ShaderBinaryManager::~ShaderBinaryManager()
{
}


Resource* ShaderBinaryManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, ShaderBinary)(path, getOwner(), m_allocator);
}


void ShaderBinaryManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<ShaderBinary*>(&resource));
}


} // namespace Lumix
