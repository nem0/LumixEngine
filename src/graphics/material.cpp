#include "graphics/material.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/timer.h"
#include "graphics/frame_buffer.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"
#include "graphics/texture.h"


namespace Lumix
{

Material::~Material()
{
	ASSERT(isEmpty());
}

void Material::apply(Renderer& renderer, PipelineInstance& pipeline)
{
	if(getState() == State::READY)
	{
		m_shader->apply();
		switch (m_depth_func)
		{
			case DepthFunc::LEQUAL:
				glDepthFunc(GL_LEQUAL);
				break;
			default:
				glDepthFunc(GL_LESS);
				break;
		}
		if (m_is_backface_culling)
		{
			glEnable(GL_CULL_FACE);
		}
		else
		{
			glDisable(GL_CULL_FACE);
		}
		for (int i = 0, c = m_textures.size(); i < c; ++i)
		{
			m_textures[i]->apply(i);
		}
		renderer.enableZTest(m_is_z_test);
		for (int i = 0, c = m_uniforms.size(); i < c; ++i)
		{
			const Uniform& uniform = m_uniforms[i];
			switch (uniform.m_type)
			{
				case Uniform::FLOAT:
					m_shader->setUniform(uniform.m_name, uniform.m_float);
					break;
				case Uniform::INT:
					m_shader->setUniform(uniform.m_name, uniform.m_int);
					break;
				case Uniform::MATRIX:
					m_shader->setUniform(uniform.m_name, uniform.m_matrix);
					break;
				case Uniform::TIME:
					m_shader->setUniform(uniform.m_name, pipeline.getScene()->getTimer()->getTimeSinceStart());
					break;
				default:
					ASSERT(false);
					break;
			}
		}
		
		if (m_shader->isShadowmapRequired())
		{
			glActiveTexture(GL_TEXTURE0 + m_textures.size());
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, pipeline.getShadowmapFramebuffer()->getDepthTexture());
			m_shader->setUniform("shadowmap", m_textures.size());
		}

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

bool Material::save(ISerializer& serializer)
{
	serializer.beginObject();
	serializer.serialize("shader", m_shader->getPath().c_str());
	for (int i = 0; i < m_textures.size(); ++i)
	{
		char path[LUMIX_MAX_PATH];
		PathUtils::getFilename(path, LUMIX_MAX_PATH, m_textures[i]->getPath().c_str());
		serializer.serialize("texture", path);
	}
	serializer.endObject();
	return false;
}

void Material::deserializeUniforms(ISerializer& serializer)
{
	serializer.deserializeArrayBegin();
	while (!serializer.isArrayEnd())
	{
		Uniform& uniform = m_uniforms.pushEmpty();
		serializer.nextArrayItem();
		serializer.deserializeObjectBegin();
		char label[256];
		while (!serializer.isObjectEnd())
		{
			serializer.deserializeLabel(label, 255);
			if (strcmp(label, "name") == 0)
			{
				serializer.deserialize(uniform.m_name, Uniform::MAX_NAME_LENGTH);
			}
			else if (strcmp(label, "int_value") == 0)
			{
				uniform.m_type = Uniform::INT;
				serializer.deserialize(uniform.m_int);
			}
			else if (strcmp(label, "float_value") == 0)
			{
				uniform.m_type = Uniform::FLOAT;
				serializer.deserialize(uniform.m_float);
			}
			else if (strcmp(label, "matrix_value") == 0)
			{
				uniform.m_type = Uniform::MATRIX;
				serializer.deserializeArrayBegin();
				for (int i = 0; i < 16; ++i)
				{
					serializer.deserializeArrayItem(uniform.m_matrix[i]);
					ASSERT(i == 15 || !serializer.isArrayEnd());
				}
				serializer.deserializeArrayEnd();
			}
			else if (strcmp(label, "time") == 0)
			{
				uniform.m_type = Uniform::TIME;
				serializer.deserialize(uniform.m_float);
			}
			else
			{
				ASSERT(false);
			}
		}
		serializer.deserializeObjectEnd();
	}
	serializer.deserializeArrayEnd();
}

void Material::removeTexture(int i)
{
	if (m_textures[i])
	{
		removeDependency(*m_textures[i]);
		m_resource_manager.get(ResourceManager::TEXTURE)->unload(*m_textures[i]);
	}
	m_textures.erase(i);
}

void Material::setTexture(int i, Texture* texture)
{ 
	if (m_textures[i])
	{
		removeDependency(*m_textures[i]);
		m_resource_manager.get(ResourceManager::TEXTURE)->unload(*m_textures[i]);
	}
	if (texture)
	{
		addDependency(*texture);
	}
	m_textures[i] = texture; 
}

void Material::addTexture(Texture* texture)
{
	if (texture)
	{
		addDependency(*texture);
	}
	m_textures.push(texture);
}

void Material::setShader(Shader* shader)
{
	if (m_shader)
	{ 
		removeDependency(*m_shader);
		m_resource_manager.get(ResourceManager::SHADER)->unload(*m_shader);
	}
	m_shader = shader;
	if (m_shader)
	{
		addDependency(*m_shader);
	}
}

void Material::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if(success)
	{
		JsonSerializer serializer(*file, JsonSerializer::READ, m_path.c_str());
		serializer.deserializeObjectBegin();
		char path[LUMIX_MAX_PATH];
		char label[256];
		char material_dir[LUMIX_MAX_PATH];
		PathUtils::getDir(material_dir, LUMIX_MAX_PATH, m_path.c_str());
		while (!serializer.isObjectEnd())
		{
			serializer.deserializeLabel(label, 255);
			if (strcmp(label, "uniforms") == 0)
			{
				deserializeUniforms(serializer);
			}
			else if (strcmp(label, "texture") == 0 || strcmp(label, "heightmap") == 0)
			{
				serializer.deserialize(path, MAX_PATH);
				if (path[0] != '\0')
				{
					base_string<char, StackAllocator<LUMIX_MAX_PATH> > texture_path;
					texture_path = material_dir;
					texture_path += path;
					Texture* texture = static_cast<Texture*>(m_resource_manager.get(ResourceManager::TEXTURE)->load(texture_path.c_str()));
					bool is_heightmap = label[0] == 'h';
					if (is_heightmap)
					{
						if (!m_textures.empty())
						{
							g_log_error.log("Renderer") << "Heightmap must be the first texture in material " << m_path.c_str();
							onFailure();
							fs.close(file);
							return;
						}
						if (texture->isReady() && !texture->getData())
						{
							g_log_error.log("Renderer") << "Heightmap " << m_path.c_str() << " can not be used as an ordinary texture";
							onFailure();
							fs.close(file);
							return;
						}
						texture->addDataReference();
					}
					m_textures.push(texture);
					addDependency(*texture);
				}
			}
			else if (strcmp(label, "shader") == 0)
			{
				serializer.deserialize(path, MAX_PATH);
				m_shader = static_cast<Shader*>(m_resource_manager.get(ResourceManager::SHADER)->load(path));
				addDependency(*m_shader);
			}
			else if (strcmp(label, "z_test") == 0)
			{
				serializer.deserialize(m_is_z_test);
			}
			else if (strcmp(label, "backface_culling") == 0)
			{
				serializer.deserialize(m_is_backface_culling);
			}
			else if (strcmp(label, "depth_func") == 0)
			{
				char tmp[30];
				serializer.deserialize(tmp, 30);
				if (strcmp(tmp, "lequal") == 0)
				{
					m_depth_func = DepthFunc::LEQUAL;
				}
				else if (strcmp(tmp, "less") == 0)
				{
					m_depth_func = DepthFunc::LESS;
				}
				else
				{
					g_log_warning.log("Renderer") << "Unknown depth function " << tmp << " in material " << m_path.c_str();
				}
			}
			else
			{
				g_log_warning.log("renderer") << "Unknown parameter " << label << " in material " << m_path.c_str();
			}
		}
		serializer.deserializeObjectEnd();

		if (!m_shader)
		{
			g_log_error.log("renderer") << "Material " << m_path.c_str() << " without a shader";
			onFailure();
			fs.close(file);
			return;
		}

		m_size = file->size();
		decrementDepCount();
	}
	else
	{
		g_log_info.log("renderer") << "Error loading material " << m_path.c_str();
		onFailure();
	}
	fs.close(file);
}


} // ~namespace Lumix
