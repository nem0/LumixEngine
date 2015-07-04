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
#include "graphics/shader.h"
#include "graphics/texture.h"


namespace Lumix
{


static const uint32_t SHADOWMAP_HASH = crc32("shadowmap");


Material::~Material()
{
	ASSERT(isEmpty());
}


void Material::updateShaderInstance()
{
	if (isReady())
	{
		if (m_shader && m_shader->isReady())
		{
			uint32_t mask = 0;
			if (m_is_alpha_cutout)
			{
				mask |= m_shader->getDefineMask("ALPHA_CUTOUT");
			}
			if (m_is_shadow_receiver)
			{
				mask |= m_shader->getDefineMask("SHADOW_RECEIVER");
			}
			for (int i = 0; i < m_shader->getTextureSlotCount(); ++i)
			{
				if (m_shader->getTextureSlot(i).m_define[0] != '\0' && m_textures[i])
				{
					mask |= m_shader->getDefineMask(m_shader->getTextureSlot(i).m_define);
				}
			}
			m_shader_instance = &m_shader->getInstance(mask);
		}
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
	serializer.serialize("backface_culling", isBackfaceCulling());
	serializer.serialize("alpha_cutout", m_is_alpha_cutout);
	serializer.serialize("shadow_receiver", m_is_shadow_receiver);
	serializer.serialize("z_test", isZTest());
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
		auto uniform_type = bgfx::UniformType::End;
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
				uniform_type = bgfx::UniformType::Int1;
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
				uniform_type = bgfx::UniformType::Mat4;
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
		uniform.m_handle = bgfx::createUniform(uniform.m_name, uniform_type);

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
	if (i >= m_texture_count)
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
		updateShaderInstance();
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
	updateShaderInstance();
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
			updateShaderInstance();
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
			serializer.deserialize(path, LUMIX_MAX_PATH, "");
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


void Material::setRenderState(bool value, uint64_t state, uint64_t mask)
{
	if (value)
	{
		m_render_states |= state;
	}
	else
	{
		m_render_states &= ~mask;
	}
}


void Material::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	m_render_states = 0;
	/*
	auto fp = fopen("shaders/vs_bump.bin", "rb");
	fseek(fp, 0, SEEK_END);
	auto s = ftell(fp);
	auto* mem = bgfx::alloc(s+1);
	fseek(fp, 0, SEEK_SET);
	fread(mem->data, s, 1, fp);
	mem->data[s] = '\0';
	auto vs = bgfx::createShader(mem);
	fclose(fp);

	fp = fopen("shaders/fs_bump.bin", "rb");
	fseek(fp, 0, SEEK_END);
	s = ftell(fp);
	mem = bgfx::alloc(s + 1);
	fseek(fp, 0, SEEK_SET);
	fread(mem->data, s, 1, fp);
	mem->data[s] = '\0';
	auto ps = bgfx::createShader(mem);
	fclose(fp);

	m_program_id = bgfx::createProgram(vs, ps, true);
	*/
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
		bool b_value;
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
			else if (strcmp(label, "shader") == 0)
			{
				serializer.deserialize(path, LUMIX_MAX_PATH, "");
				setShader(static_cast<Shader*>(m_resource_manager.get(ResourceManager::SHADER)->load(Path(path))));
			}
			else if (strcmp(label, "z_test") == 0)
			{
				serializer.deserialize(b_value, true);
				enableZTest(b_value);
			}
			else if (strcmp(label, "backface_culling") == 0)
			{
				serializer.deserialize(b_value, true);
				enableBackfaceCulling(b_value);
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
