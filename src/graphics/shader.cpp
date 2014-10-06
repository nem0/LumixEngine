#include "graphics/shader.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"
#include "graphics/shader_manager.h"


namespace Lumix
{


Shader::Shader(const Path& path, ResourceManager& resource_manager)
	: Resource(path, resource_manager)
	, m_is_shadowmap_required(true)
{
}


Shader::~Shader()
{
	for(int i = 0; i < m_combinations.size(); ++i)
	{	
		glDeleteProgram(m_combinations[i]->m_program_id);
		glDeleteShader(m_combinations[i]->m_vertex_id);
		glDeleteShader(m_combinations[i]->m_fragment_id);
		LUMIX_DELETE(m_combinations[i]);
	}
}


GLint Shader::getUniformLocation(const char* name, uint32_t name_hash)
{
	Array<CachedUniform>& uniforms = m_current_combination->m_uniforms;
	for (int i = 0, c = uniforms.size(); i < c; ++i)
	{
		if (uniforms[i].m_name_hash == name_hash)
		{
			return uniforms[i].m_location;
		}
	}
	ASSERT(isReady());
	CachedUniform& unif = uniforms.pushEmpty();
	unif.m_name_hash = name_hash;
	unif.m_location = glGetUniformLocation(m_current_combination->m_program_id, name);
	return unif.m_location;
}


Shader::Combination* Shader::getCombination(uint32_t hash) const
{
	for(int i = 0, c = m_combinations.size(); i < c; ++i)
	{
		if(m_combinations[i]->m_hash == hash)
		{
			return m_combinations[i];
		}
	}
	return NULL;
}


void Shader::createCombination(const char* defines)
{
	ASSERT(isReady());
	uint32_t hash = defines[0] == '\0' ? 0 : crc32(defines);
	if(!getCombination(hash))
	{
		Combination* combination = LUMIX_NEW(Combination);
		m_combinations.push(combination);
		combination->m_defines = defines;
		combination->m_program_id = glCreateProgram();
		combination->m_hash = hash;

		combination->m_vertex_id = glCreateShader(GL_VERTEX_SHADER);
		const GLchar* vs_strings[] = { "#define VERTEX_SHADER\n", defines, m_source.c_str() };
		glShaderSource(combination->m_vertex_id, sizeof(vs_strings) / sizeof(vs_strings[0]), vs_strings, NULL);
		glCompileShader(combination->m_vertex_id);
		glAttachShader(combination->m_program_id, combination->m_vertex_id);

		combination->m_fragment_id = glCreateShader(GL_FRAGMENT_SHADER);
		const GLchar* fs_strings[] = { "#define FRAGMENT_SHADER\n", defines, m_source.c_str() };
		glShaderSource(combination->m_fragment_id, sizeof(fs_strings) / sizeof(fs_strings[0]), fs_strings, NULL);
		glCompileShader(combination->m_fragment_id);
		glAttachShader(combination->m_program_id, combination->m_fragment_id);

		glLinkProgram(combination->m_program_id);
		GLint link_status;
		glGetProgramiv(combination->m_program_id, GL_LINK_STATUS, &link_status);
		if (link_status != GL_TRUE)
		{
			g_log_error.log("renderer") << "Could not link shader " << m_path.c_str();
			char buffer[1024];
			GLsizei info_log_len;
			glGetProgramInfoLog(combination->m_program_id, sizeof(buffer), &info_log_len, buffer);
			if(info_log_len)
			{
				g_log_error.log("renderer") << "Shader error log: " << buffer;
			}
			return;
		}
		else
		{
			char buffer[1024];
			GLsizei info_log_len;
			glGetProgramInfoLog(combination->m_program_id, sizeof(buffer), &info_log_len, buffer);
			if(info_log_len)
			{
				g_log_info.log("renderer") << "Shader log: " << buffer;
			}
		}
		
		for (int i = 0; i < m_attributes.size(); ++i)
		{
			combination->m_vertex_attributes_ids[i] = glGetAttribLocation(combination->m_program_id, m_attributes[i].c_str());
		}
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::WORLD_MATRIX] = glGetUniformLocation(combination->m_program_id, "world_matrix");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::GRASS_MATRICES] = glGetUniformLocation(combination->m_program_id, "grass_matrices");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::MORPH_CONST] = glGetUniformLocation(combination->m_program_id, "morph_const");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::QUAD_SIZE] = glGetUniformLocation(combination->m_program_id, "quad_size");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::QUAD_MIN] = glGetUniformLocation(combination->m_program_id, "quad_min");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::AMBIENT_COLOR] = glGetUniformLocation(combination->m_program_id, "ambient_color");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::AMBIENT_INTENSITY] = glGetUniformLocation(combination->m_program_id, "ambient_intensity");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::DIFFUSE_COLOR] = glGetUniformLocation(combination->m_program_id, "diffuse_color");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::DIFFUSE_INTENSITY] = glGetUniformLocation(combination->m_program_id, "diffuse_intensity");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::FOG_COLOR] = glGetUniformLocation(combination->m_program_id, "fog_color");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::FOG_DENSITY] = glGetUniformLocation(combination->m_program_id, "fog_density");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::SHADOWMAP_SPLITS] = glGetUniformLocation(combination->m_program_id, "shadowmap_splits");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::VIEW_MATRIX] = glGetUniformLocation(combination->m_program_id, "view_matrix");
		combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::PROJECTION_MATRIX] = glGetUniformLocation(combination->m_program_id, "projection_matrix");
	}
}


void Shader::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if(success)
	{
		JsonSerializer serializer(*file, JsonSerializer::READ, m_path.c_str());
		serializer.deserializeObjectBegin();
		m_attributes.clear();
		while (!serializer.isObjectEnd())
		{
			char label[256];
			serializer.deserializeLabel(label, 255);
			if (strcmp(label, "attributes") == 0)
			{
				serializer.deserializeArrayBegin();
				while (!serializer.isArrayEnd())
				{
					serializer.deserializeArrayItem(label, sizeof(label));
					m_attributes.push(string(label)); /// TODO emplace when it is merged
				}
				serializer.deserializeArrayEnd();
			}
			else if (strcmp(label, "shadowmap_required") == 0)
			{
				serializer.deserialize(m_is_shadowmap_required);
			}
		}
		serializer.deserializeObjectEnd();
		
		int32_t size = serializer.getRestOfFileSize();
		ShaderManager* manager = static_cast<ShaderManager*>(getResourceManager().get(ResourceManager::SHADER));
		char* buf = reinterpret_cast<char*>(manager->getBuffer(size + 1));
		serializer.deserializeRawString(buf, size);
		buf[size] = '\0';
		m_source = buf;
		m_size = file->size();
		decrementDepCount();
		if(!m_combinations.empty())
		{
			Array<Combination*> old_combinations;
			for(int i = 0; i < m_combinations.size(); ++i)
			{
				old_combinations.push(m_combinations[i]);
			}
			m_combinations.clear();
			for(int i = 0; i < old_combinations.size(); ++i)
			{
				createCombination(old_combinations[i]->m_defines.c_str());
			}
			for(int i = 0; i < old_combinations.size(); ++i)
			{
				LUMIX_DELETE(old_combinations[i]);
			}
		}
		else
		{
			createCombination("");
		}
	}
	else
	{
		g_log_error.log("renderer") << "Could not load shader " << m_path.c_str();
		onFailure();
	}

	fs.close(file);
}

void Shader::doUnload(void)
{
	for(int i = 0; i < m_combinations.size(); ++i)
	{
		m_combinations[i]->m_uniforms.clear();
		glDeleteProgram(m_combinations[i]->m_program_id);
		glDeleteShader(m_combinations[i]->m_vertex_id);
		glDeleteShader(m_combinations[i]->m_fragment_id);
	}
	m_size = 0;
	onEmpty();
}

FS::ReadCallback Shader::getReadCallback()
{
	FS::ReadCallback cb;
	cb.bind<Shader, &Shader::loaded>(this);
	return cb;
}


} // ~namespace Lumix
