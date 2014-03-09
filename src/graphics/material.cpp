#include "graphics/material.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/json_serializer.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"
#include "graphics/texture.h"


namespace Lux
{

void Material::apply()
{
	if(getState() == State::READY)
	{
		m_shader->apply();
		for(int i = 0, c = m_textures.size(); i < c; ++i)
		{
			m_textures[i]->apply(i);
		}
	}
}

void Material::doUnload(void)
{
	TODO("Implement Material Unload");
}

void Material::doReload(void)
{
	TODO("Implement Material Reload");
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
//		m_textures.push(m_renderer.loadTexture(path));
		
		serializer.deserialize("shader", path, MAX_PATH);
		m_shader = static_cast<Shader*>(m_resource_manager.get(ResourceManager::SHADER)->load(path));

		onReady();
	}

	fs.close(file);
}


} // ~namespace Lux
