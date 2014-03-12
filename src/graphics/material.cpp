#include "graphics/material.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"
#include "graphics/texture.h"


namespace Lux
{


Material::Material(Renderer& renderer)
	: m_renderer(renderer)
{
	m_is_ready = false;
}


void Material::apply()
{
	if(m_is_ready)
	{
		m_shader->apply();
		for(int i = 0, c = m_textures.size(); i < c; ++i)
		{
			m_textures[i]->apply(i);
		}
	}
}


void Material::load(const char* path, FS::FileSystem& file_system)
{
	FS::ReadCallback cb;
	cb.bind<Material, &Material::loaded>(this);
	file_system.openAsync(file_system.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, cb);
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
			m_textures.push(m_renderer.loadTexture(path));
		}
		
		serializer.deserialize("shader", path, MAX_PATH);
		m_shader = m_renderer.loadShader(path);
		m_is_ready = true;

	}
	else
	{
		g_log_info.log("loading", "Error loading material.");
	}
	fs.close(file);
}


} // ~namespace Lux
