#include "core/lux.h"
#include "graphics/shader_manager.h"

#include "core/resource.h"
#include "graphics/shader.h"

namespace Lux
{
	ShaderManager::ShaderManager()
		: ResourceManagerBase()
	{
		m_buffer = NULL;
		m_buffer_size = -1;
	}


	ShaderManager::~ShaderManager()
	{
		LUX_DELETE_ARRAY(m_buffer);
	}


	Resource* ShaderManager::createResource(const Path& path)
	{
		return LUX_NEW(Shader)(path, getOwner());
	}

	void ShaderManager::destroyResource(Resource& resource)
	{
		LUX_DELETE(static_cast<Shader*>(&resource));
	}

	char* ShaderManager::getBuffer(int32_t size)
	{
		if (m_buffer_size < size)
		{
			LUX_DELETE_ARRAY(m_buffer);
			m_buffer = NULL;
			m_buffer_size = -1;
		}
		if (m_buffer == NULL)
		{
			m_buffer = LUX_NEW_ARRAY(uint8_t, size);
			m_buffer_size = size;
		}
		return (char*)m_buffer;
	}
}