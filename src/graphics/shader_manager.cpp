#include "core/lumix.h"
#include "graphics/shader_manager.h"

#include "core/resource.h"
#include "graphics/shader.h"

namespace Lumix
{
	ShaderManager::ShaderManager()
		: ResourceManagerBase()
	{
		m_buffer = NULL;
		m_buffer_size = -1;
	}


	ShaderManager::~ShaderManager()
	{
		LUMIX_DELETE_ARRAY(m_buffer);
	}


	Resource* ShaderManager::createResource(const Path& path)
	{
		return LUMIX_NEW(Shader)(path, getOwner());
	}

	void ShaderManager::destroyResource(Resource& resource)
	{
		LUMIX_DELETE(static_cast<Shader*>(&resource));
	}

	uint8_t* ShaderManager::getBuffer(int32_t size)
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