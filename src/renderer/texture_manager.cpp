#include "engine/lumix.h"
#include "renderer/texture_manager.h"

#include "engine/core/resource.h"
#include "renderer/texture.h"

namespace Lumix
{
	TextureManager::TextureManager(IAllocator& allocator)
		: ResourceManagerBase(allocator)
		, m_allocator(allocator)
	{
		m_buffer = nullptr;
		m_buffer_size = -1;
	}


	TextureManager::~TextureManager()
	{
		m_allocator.deallocate(m_buffer);
	}


	Resource* TextureManager::createResource(const Path& path)
	{
		return LUMIX_NEW(m_allocator, Texture)(path, getOwner(), m_allocator);
	}

	void TextureManager::destroyResource(Resource& resource)
	{
		LUMIX_DELETE(m_allocator, static_cast<Texture*>(&resource));
	}

	uint8* TextureManager::getBuffer(int32 size)
	{
		if (m_buffer_size < size)
		{
			m_allocator.deallocate(m_buffer);
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
}