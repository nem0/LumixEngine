#include "graphics/material.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/json_serializer.h"
#include "core/resource_manager.h"
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

void Material::doLoad(void)
{
	FS::ReadCallback cb;
	cb.bind<Material, &Material::loaded>(this);

	FS::FileSystem& fs = m_resource_manager.getFileSystem();
	fs.openAsync(fs.getDefaultDevice(), m_path, FS::Mode::OPEN | FS::Mode::READ, cb);
}

void Material::doUnload(void)
{
	TODO("Implement Material Unload");
}

void Material::doReload(void)
{
	TODO("Implement Material Reload");
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
//		m_shader = m_renderer.loadShader(path);

		onReady();

	}

	fs.close(file);
}


} // ~namespace Lux
