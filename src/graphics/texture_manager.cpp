#include "core/lumix.h"
#include "graphics/texture_manager.h"

#include "core/resource.h"
#include "graphics/texture.h"

namespace Lumix
{
	TextureManager::TextureManager(IAllocator& allocator)
		: ResourceManagerBase(allocator)
		, m_allocator(allocator)
	{
		m_buffer = NULL;
		m_buffer_size = -1;
	}


	TextureManager::~TextureManager()
	{
		m_allocator.deallocate(m_buffer);
	}


	Resource* TextureManager::createResource(const Path& path)
	{
		return m_allocator.newObject<Texture>(path, getOwner());
	}

	void TextureManager::destroyResource(Resource& resource)
	{
		m_allocator.deleteObject(static_cast<Texture*>(&resource));
	}

	uint8_t* TextureManager::getBuffer(int32_t size)
	{
		if (m_buffer_size < size)
		{
			m_allocator.deallocate(m_buffer);
			m_buffer = NULL;
			m_buffer_size = -1;
		}
		if (m_buffer == NULL)
		{
			m_buffer = (uint8_t*)m_allocator.allocate(sizeof(uint8_t) * size);
			m_buffer_size = size;
		}
		return m_buffer;
	}
}