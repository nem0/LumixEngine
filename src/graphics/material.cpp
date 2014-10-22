#include "graphics/material.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/profiler.h"
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


static const uint32_t SHADOWMAP_HASH = crc32("shadowmap");


Material::~Material()
{
	ASSERT(isEmpty());
}

void Material::apply(Renderer& renderer, PipelineInstance& pipeline) const
{
	PROFILE_FUNCTION();
	if(getState() == State::READY)
	{
		renderer.applyShader(*m_shader, m_shader_combination);
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
			m_textures[i].m_texture->apply(i);
		}
		renderer.enableAlphaToCoverage(m_is_alpha_to_coverage);
		renderer.enableZTest(m_is_z_test);
		for (int i = 0, c = m_uniforms.size(); i < c; ++i)
		{
			const Uniform& uniform = m_uniforms[i];
			switch (uniform.m_type)
			{
				case Uniform::FLOAT:
					renderer.setUniform(*m_shader, uniform.m_name, uniform.m_name_hash, uniform.m_float);
					break;
				case Uniform::INT:
					renderer.setUniform(*m_shader, uniform.m_name, uniform.m_name_hash, uniform.m_int);
					break;
				case Uniform::MATRIX:
					renderer.setUniform(*m_shader, uniform.m_name, uniform.m_name_hash, uniform.m_matrix);
					break;
				case Uniform::TIME:
					renderer.setUniform(*m_shader, uniform.m_name, uniform.m_name_hash, pipeline.getScene()->getTimer()->getTimeSinceStart());
					break;
				default:
					ASSERT(false);
					break;
			}
		}
		
		if (m_shader->isShadowmapRequired())
		{
			glActiveTexture(GL_TEXTURE0 + m_textures.size());
			glBindTexture(GL_TEXTURE_2D, pipeline.getShadowmapFramebuffer()->getDepthTexture());
			renderer.setUniform(*m_shader, "shadowmap", SHADOWMAP_HASH, m_textures.size());
		}

	}
}


void Material::updateShaderCombination()
{
	static const int MAX_DEFINES_LENGTH = 1024;
	char defines[MAX_DEFINES_LENGTH];
	copyString(defines, MAX_DEFINES_LENGTH, m_is_alpha_cutout ? "#define ALPHA_CUTOUT\n" : "" );
	catCString(defines, MAX_DEFINES_LENGTH, m_is_shadow_receiver ? "#define SHADOW_RECEIVER\n" : "" );
	m_shader_combination = crc32(defines);
	if(m_shader && m_shader->isReady())
	{
		m_shader->createCombination(defines);
	}
}


void Material::doUnload(void)
{
	setShader(NULL);

	ResourceManagerBase* texture_manager = m_resource_manager.get(ResourceManager::TEXTURE);
	for(int i = 0; i < m_textures.size(); i++)
	{
		removeDependency(*m_textures[i].m_texture);
		texture_manager->unload(*m_textures[i].m_texture);
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
		PathUtils::getFilename(path, LUMIX_MAX_PATH, m_textures[i].m_texture->getPath().c_str());
		serializer.beginObject("texture");
		serializer.serialize("source", path);
		serializer.serialize("keep_data", m_textures[i].m_keep_data);
		if (m_textures[i].m_uniform[0] != '\0')
		{
			serializer.serialize("uniform", m_textures[i].m_uniform);
		}
		serializer.endObject();
	}
	serializer.beginArray("uniforms");
	for (int i = 0; i < m_uniforms.size(); ++i)
	{
		serializer.beginObject();
		serializer.serialize("name", m_uniforms[i].m_name);
		serializer.serialize("is_editable", m_uniforms[i].m_is_editable);
		switch (m_uniforms[i].m_type)
		{
			case Uniform::FLOAT:
				serializer.serialize("float_value", m_uniforms[i].m_float);
				break;
			case Uniform::TIME:
				serializer.serialize("time", m_uniforms[i].m_float);
				break;
			case Uniform::INT:
				serializer.serialize("int_value", m_uniforms[i].m_int);
				break;
			case Uniform::MATRIX:
				serializer.beginArray("matrix_value");
				for (int j = 0; j < 16; ++j)
				{
					serializer.serializeArrayItem(m_uniforms[i].m_matrix[j]);
				}
				serializer.endArray();
				break;
			default:
				ASSERT(false);
				break;
		}
		serializer.endObject();
	}
	serializer.endArray();
	serializer.serialize("alpha_to_coverage", m_is_alpha_to_coverage);
	serializer.serialize("backface_culling", m_is_backface_culling);
	serializer.serialize("alpha_cutout", m_is_alpha_cutout);
	serializer.serialize("shadow_receiver", m_is_shadow_receiver);
	serializer.serialize("z_test", m_is_z_test);
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
			if (strcmp(label, "is_editable") == 0)
			{
				serializer.deserialize(uniform.m_is_editable);
			}
			else if (strcmp(label, "name") == 0)
			{
				serializer.deserialize(uniform.m_name, Uniform::MAX_NAME_LENGTH);
				uniform.m_name_hash = crc32(uniform.m_name);
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
	if (m_textures[i].m_texture)
	{
		removeDependency(*m_textures[i].m_texture);
		m_resource_manager.get(ResourceManager::TEXTURE)->unload(*m_textures[i].m_texture);
	}
	m_textures.erase(i);
}

Texture* Material::getTextureByUniform(const char* uniform) const
{
	for (int i = 0; i < m_textures.size(); ++i)
	{
		if (strcmp(m_textures[i].m_uniform, uniform) == 0)
		{
			return m_textures[i].m_texture;
		}
	}
	return NULL;
}

void Material::setTexture(int i, Texture* texture)
{ 
	if (m_textures[i].m_texture)
	{
		removeDependency(*m_textures[i].m_texture);
		m_resource_manager.get(ResourceManager::TEXTURE)->unload(*m_textures[i].m_texture);
	}
	if (texture)
	{
		addDependency(*texture);
	}
	m_textures[i].m_texture = texture;
}

void Material::addTexture(Texture* texture)
{
	if (texture)
	{
		addDependency(*texture);
	}
	m_textures.pushEmpty().m_texture = texture;
}

void Material::shaderLoaded(Resource::State, Resource::State)
{
	updateShaderCombination();
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
		m_shader->getObserverCb().bind<Material, &Material::shaderLoaded>(this);
		if(m_shader->isReady())
		{
			shaderLoaded(Resource::State::READY, Resource::State::READY);
		}
	}
}

bool Material::deserializeTexture(ISerializer& serializer, const char* material_dir)
{
	char path[LUMIX_MAX_PATH];
	TextureInfo& info = m_textures.pushEmpty();
	serializer.deserializeObjectBegin();
	char label[256];
	bool data_kept = false;
	while (!serializer.isObjectEnd())
	{
		serializer.deserializeLabel(label, sizeof(label));
		if (strcmp(label, "source") == 0)
		{
			serializer.deserialize(path, MAX_PATH);
			if (path[0] != '\0')
			{
				base_string<char, StackAllocator<LUMIX_MAX_PATH> > texture_path;
				texture_path = material_dir;
				texture_path += path;
				info.m_texture = static_cast<Texture*>(m_resource_manager.get(ResourceManager::TEXTURE)->load(texture_path.c_str()));
				addDependency(*info.m_texture);

				if (info.m_keep_data)
				{
					if (info.m_texture->isReady() && !info.m_texture->getData())
					{
						g_log_error.log("Renderer") << "Cannot keep data for texture " << m_path.c_str() << "because the texture has already been loaded.";
						return false;
					}
					if (!data_kept)
					{
						info.m_texture->addDataReference();
						data_kept = true;
					}
				}
			}
		}
		else if (strcmp("uniform", label) == 0)
		{
			Uniform& uniform = m_uniforms.pushEmpty();
			serializer.deserialize(uniform.m_name, Uniform::MAX_NAME_LENGTH);
			copyString(info.m_uniform, sizeof(info.m_uniform), uniform.m_name);
			uniform.m_name_hash = crc32(uniform.m_name);
			uniform.m_type = Uniform::INT;
			uniform.m_int = info.m_texture ? m_textures.size() - 1 : m_textures.size();
		}
		else if (strcmp("keep_data", label) == 0)
		{
			serializer.deserialize(info.m_keep_data);
			if (info.m_keep_data && info.m_texture)
			{
				if (info.m_texture->isReady() && !info.m_texture->getData())
				{
					g_log_error.log("Renderer") << "Cannot keep data for texture " << info.m_texture->getPath().c_str() << ", it's already loaded.";
					return false;
				}
				if (!data_kept)
				{
					data_kept = true;
					info.m_texture->addDataReference();
				}
			}
		}
		else
		{
			g_log_error.log("Renderer") << "Unknown data \"" << label << "\" in material " << m_path.c_str();
			return false;
		}
	}
	serializer.deserializeObjectEnd();
	return true;
}

void Material::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	PROFILE_FUNCTION();
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
			else if (strcmp(label, "texture") == 0)
			{
				if (!deserializeTexture(serializer, material_dir))
				{
					onFailure();
					fs.close(file);
					return;
				}
				
			}
			else if (strcmp(label, "alpha_cutout") == 0)
			{
				serializer.deserialize(m_is_alpha_cutout);
			}
			else if (strcmp(label, "shadow_receiver") == 0)
			{
				serializer.deserialize(m_is_shadow_receiver);
			}
			else if (strcmp(label, "alpha_to_coverage") == 0)
			{
				serializer.deserialize(m_is_alpha_to_coverage);
			}
			else if (strcmp(label, "shader") == 0)
			{
				serializer.deserialize(path, MAX_PATH);
				setShader(static_cast<Shader*>(m_resource_manager.get(ResourceManager::SHADER)->load(path)));
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
