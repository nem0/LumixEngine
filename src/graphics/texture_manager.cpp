#include "core/lumix.h"
#include "graphics/texture_manager.h"

#include "core/resource.h"
#include "graphics/texture.h"

namespace Lumix
{
	TextureManager::TextureManager()
		: ResourceManagerBase()
	{
		m_buffer = NULL;
		m_buffer_size = -1;
	}


	TextureManager::~TextureManager()
	{
		LUMIX_DELETE_ARRAY(m_buffer);
	}


	Resource* TextureManager::createResource(const Path& path)
	{
		return LUMIX_NEW(Texture)(path, getOwner());
	}

	void TextureManager::destroyResource(Resource& resource)
	{
		LUMIX_DELETE(static_cast<Texture*>(&resource));
	}

	uint8_t* TextureManager::getBuffer(int32_t size)
	{
		if (m_buffer_size < size)
		{
			LUMIX_DELETE_ARRAY(m_buffer);
			m_buffer = NULL;
			m_buffer_size = -1;
		}
		if (m_buffer == NULL)
		{
			m_buffer = LUMIX_NEW_ARRAY(uint8_t, size);
			m_buffer_size = size;
		}
		return m_buffer;
	}
}