#include "graphics/material.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/json_serializer.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"
#include "graphics/texture.h"


namespace Lux
{


void Material::apply()
{
	/// TODO shader
	//m_shader->apply();
	for(int i = 0, c = m_textures.size(); i < c; ++i)
	{
		m_textures[i]->apply(i);
	}
}


void Material::load(const char* path, FS::FileSystem& file_system)
{
	FS::ReadCallback cb;
	cb.bind<Material, &Material::loaded>(this);
	file_system.openAsync(file_system.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, cb);
}


void Material::loaded(FS::IFile* file, bool success)
{
	if(success)
	{
		JsonSerializer serializer(*file, JsonSerializer::READ);
		char texture_path[MAX_PATH];
		serializer.deserialize("texture", texture_path, MAX_PATH);
		m_textures.push(m_renderer.loadTexture(texture_path));
		//m_textures.push_back(res_manager.texture_manager->load(texture_path));
	}
	/// TODO close file somehow
}


} // ~namespace Lux
