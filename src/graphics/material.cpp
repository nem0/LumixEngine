#include "graphics/material.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"
#include "graphics/texture.h"


namespace Lux
{

void Material::apply(Renderer& renderer)
{
	if(getState() == State::READY)
	{
		m_shader->apply();
		for(int i = 0, c = m_textures.size(); i < c; ++i)
		{
			m_textures[i]->apply(i);
		}
		renderer.enableZTest(m_is_z_test);
	}
}

void Material::doUnload(void)
{
	if(m_shader)
	{
		removeDependency(*m_shader);
		m_resource_manager.get(ResourceManager::SHADER)->unload(*m_shader);
		m_shader = NULL;
	}

	ResourceManagerBase* texture_manager = m_resource_manager.get(ResourceManager::TEXTURE);
	for(int i = 0; i < m_textures.size(); i++)
	{
		removeDependency(*m_textures[i]);
		texture_manager->unload(*m_textures[i]);
	}
	m_textures.clear();

	m_size = 0;
	onEmpty();
}

FS::ReadCallback Material::getReadCallback()
{
	FS::ReadCallback rc;
	rc.bind<Material, &Material::loaded>(this);
	return rc;
}

void Material::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if(success)
	{
		JsonSerializer serializer(*file, JsonSerializer::READ);
		char path[MAX_PATH];
		serializer.deserialize("texture", path, MAX_PATH);
		if(path[0] != '\0')
		{
			Texture* texture = static_cast<Texture*>(m_resource_manager.get(ResourceManager::TEXTURE)->load(path));
			m_textures.push(texture);
			addDependency(*texture);
		}
		
		serializer.deserialize("shader", path, MAX_PATH);
		m_shader = static_cast<Shader*>(m_resource_manager.get(ResourceManager::SHADER)->load(path));
		addDependency(*m_shader);

		serializer.deserialize("z_test", m_is_z_test);

		m_size = file->size();
		decrementDepCount();
	}
	else
	{
		g_log_info.log("loading", "Error loading material.");
	}
	fs.close(file);
}


} // ~namespace Lux
