#include "lumix.h"
#include "renderer/shader_manager.h"

#include "core/resource.h"
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
		m_allocator.deleteObject(m_buffer);
	}


	Resource* ShaderManager::createResource(const Path& path)
	{
		return m_allocator.newObject<Shader>(path, getOwner(), m_allocator);
	}

	void ShaderManager::destroyResource(Resource& resource)
	{
		m_allocator.deleteObject(static_cast<Shader*>(&resource));
	}

	uint8_t* ShaderManager::getBuffer(int32_t size)
	{
		if (m_buffer_size < size)
		{
			m_allocator.deleteObject(m_buffer);
			m_buffer = nullptr;
			m_buffer_size = -1;
		}
		if (m_buffer == nullptr)
		{
			m_buffer = (uint8_t*)m_allocator.allocate(sizeof(uint8_t) * size);
			m_buffer_size = size;
		}
		return m_buffer;
	}
}