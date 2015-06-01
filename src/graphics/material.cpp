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
		for (int i = 0, c = m_texture_count; i < c; ++i)
		{
			if (m_textures[i])
			{
				m_textures[i]->apply(i);
			}
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
					renderer.setUniform(*m_shader, uniform.m_name, uniform.m_name_hash, pipeline.getScene()->getTime());
					break;
				default:
					ASSERT(false);
					break;
			}
		}
	}
}


void Material::updateShaderCombination()
{
	if (isReady())
	{
		static const int MAX_DEFINES_LENGTH = 1024;
		char defines[MAX_DEFINES_LENGTH];
		copyString(defines, MAX_DEFINES_LENGTH, m_is_alpha_cutout ? "#define ALPHA_CUTOUT\n" : "");
		catCString(defines, MAX_DEFINES_LENGTH, m_is_shadow_receiver ? "#define SHADOW_RECEIVER\n" : "");
		if (m_shader && m_shader->isReady())
		{
			for (int i = 0; i < m_shader->getTextureSlotCount(); ++i)
			{
				if (m_shader->getTextureSlot(i).m_define[0] != '\0' && m_textures[i])
				{
					catCString(defines, MAX_DEFINES_LENGTH, "#define ");
					catCString(defines, MAX_DEFINES_LENGTH, m_shader->getTextureSlot(i).m_define);
					catCString(defines, MAX_DEFINES_LENGTH, "\n");
				}
			}
			m_shader->createCombination(defines);
		}
		m_shader_combination = crc32(defines);
	}
}


void Material::doUnload(void)
{
	setShader(NULL);

	ResourceManagerBase* texture_manager = m_resource_manager.get(ResourceManager::TEXTURE);
	for (int i = 0; i < m_texture_count; i++)
	{
		if (m_textures[i])
		{
			removeDependency(*m_textures[i]);
			texture_manager->unload(*m_textures[i]);
		}
	}
	m_texture_count = 0;

	m_size = 0;
	onEmpty();
}


bool Material::save(JsonSerializer& serializer)
{
	serializer.beginObject();
	serializer.serialize("shader", m_shader ? m_shader->getPath().c_str() : "");
	for (int i = 0; i < m_texture_count; ++i)
	{
		char path[LUMIX_MAX_PATH];
		if (m_textures[i])
		{
			PathUtils::getFilename(path, LUMIX_MAX_PATH, m_textures[i]->getPath().c_str());
		}
		else
		{
			path[0] = '\0';
		}
		serializer.beginObject("texture");
		serializer.serialize("source", path);
		serializer.endObject();
	}
	serializer.beginArray("uniforms");
	for (int i = 0; i < m_uniforms.size(); ++i)
	{
		serializer.beginObject();
		serializer.serialize("name", m_uniforms[i].m_name);
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

void Material::deserializeUniforms(JsonSerializer& serializer)
{
	serializer.deserializeArrayBegin();
	m_uniforms.clear();
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
				serializer.deserialize(uniform.m_name, Uniform::MAX_NAME_LENGTH, "");
				uniform.m_name_hash = crc32(uniform.m_name);
			}
			else if (strcmp(label, "int_value") == 0)
			{
				uniform.m_type = Uniform::INT;
				serializer.deserialize(uniform.m_int, 0);
			}
			else if (strcmp(label, "float_value") == 0)
			{
				uniform.m_type = Uniform::FLOAT;
				serializer.deserialize(uniform.m_float, 0);
			}
			else if (strcmp(label, "matrix_value") == 0)
			{
				uniform.m_type = Uniform::MATRIX;
				serializer.deserializeArrayBegin();
				for (int i = 0; i < 16; ++i)
				{
					serializer.deserializeArrayItem(uniform.m_matrix[i], 0);
					ASSERT(i == 15 || !serializer.isArrayEnd());
				}
				serializer.deserializeArrayEnd();
			}
			else if (strcmp(label, "time") == 0)
			{
				uniform.m_type = Uniform::TIME;
				serializer.deserialize(uniform.m_float, 0);
			}
			else
			{
				g_log_warning.log("material") << "Unknown label \"" << label << "\"";
			}
		}
		serializer.deserializeObjectEnd();
	}
	serializer.deserializeArrayEnd();
}


void Material::setTexturePath(int i, const Path& path)
{
	if (path.length() == 0)
	{
		setTexture(i, nullptr);
	}
	else
	{
		Texture* texture = static_cast<Texture*>(m_resource_manager.get(ResourceManager::TEXTURE)->load(path));
		setTexture(i, texture);
	}
}


void Material::setTexture(int i, Texture* texture)
{ 
	Texture* old_texture = i < m_texture_count ? m_textures[i] : nullptr;
	if (texture)
	{
		addDependency(*texture);
	}
	m_textures[i] = texture;
	if (i <= m_texture_count)
	{
		m_texture_count = i + 1;
	}
	if (old_texture)
	{
		removeDependency(*old_texture);
		m_resource_manager.get(ResourceManager::TEXTURE)->unload(*old_texture);
	}
	if (isReady())
	{
		updateShaderCombination();
	}
}


void Material::setShader(const Path& path)
{
	Shader* shader = static_cast<Shader*>(m_resource_manager.get(ResourceManager::SHADER)->load(path));
	setShader(shader);
}


void Material::onReady()
{
	Resource::onReady();
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

		if (m_shader->isReady())
		{
			updateShaderCombination();
		}
	}
}

const char* Material::getTextureUniform(int i)
{
	if (i < m_shader->getTextureSlotCount())
	{
		return m_shader->getTextureSlot(i).m_uniform;
	}
	return "";
}

Texture* Material::getTextureByUniform(const char* uniform) const
{
	for (int i = 0, c = m_shader->getTextureSlotCount(); i < c; ++i)
	{
		if (strcmp(m_shader->getTextureSlot(i).m_uniform, uniform) == 0)
		{
			return m_textures[i];
		}
	}
	return nullptr;
}

bool Material::deserializeTexture(JsonSerializer& serializer, const char* material_dir)
{
	char path[LUMIX_MAX_PATH];
	serializer.deserializeObjectBegin();
	char label[256];
	bool keep_data = false;
	while (!serializer.isObjectEnd())
	{
		serializer.deserializeLabel(label, sizeof(label));
		if (strcmp(label, "source") == 0)
		{
			serializer.deserialize(path, MAX_PATH, "");
			if (path[0] != '\0')
			{
				char texture_path[LUMIX_MAX_PATH];
				copyString(texture_path, sizeof(texture_path), material_dir);
				catCString(texture_path, sizeof(texture_path), path);
				m_textures[m_texture_count] = static_cast<Texture*>(m_resource_manager.get(ResourceManager::TEXTURE)->load(Path(texture_path)));
				addDependency(*m_textures[m_texture_count]);
			}
		}
		else if (strcmp(label, "keep_data") == 0)
		{
			keep_data = true;
		}
		else
		{
			g_log_warning.log("Renderer") << "Unknown data \"" << label << "\" in material " << m_path.c_str();
			return false;
		}
	}
	if (keep_data)
	{
		m_textures[m_texture_count]->addDataReference();
	}
	serializer.deserializeObjectEnd();
	++m_texture_count;
	return true;
}

void Material::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	PROFILE_FUNCTION();
	if(success)
	{
		m_uniforms.clear();
		JsonSerializer serializer(*file, JsonSerializer::READ, m_path.c_str(), m_allocator);
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
				serializer.deserialize(m_is_alpha_cutout, false);
			}
			else if (strcmp(label, "shadow_receiver") == 0)
			{
				serializer.deserialize(m_is_shadow_receiver, true);
			}
			else if (strcmp(label, "alpha_to_coverage") == 0)
			{
				serializer.deserialize(m_is_alpha_to_coverage, false);
			}
			else if (strcmp(label, "shader") == 0)
			{
				serializer.deserialize(path, LUMIX_MAX_PATH, "");
				setShader(static_cast<Shader*>(m_resource_manager.get(ResourceManager::SHADER)->load(Path(path))));
			}
			else if (strcmp(label, "z_test") == 0)
			{
				serializer.deserialize(m_is_z_test, true);
			}
			else if (strcmp(label, "backface_culling") == 0)
			{
				serializer.deserialize(m_is_backface_culling, true);
			}
			else if (strcmp(label, "depth_func") == 0)
			{
				char tmp[30];
				serializer.deserialize(tmp, 30, "lequal");
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
