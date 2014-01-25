#include "graphics/material.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "graphics/shader.h"
#include "graphics/texture.h"


namespace Lux
{


void Material::apply()
{
	m_shader->apply();
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
		int str_len = 0;
		file->read(&str_len, sizeof(str_len));
		char texture_path[MAX_PATH];
		file->read(texture_path, str_len);
		texture_path[str_len] = 0;
		//m_textures.push_back(res_manager.texture_manager->load(texture_path));
	}
	/// TODO close file somehow
}


} // ~namespace Lux
