#include "graphics/shader.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"
#include "graphics/renderer.h"
#include "graphics/shader_manager.h"
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


Shader::Shader(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_renderer(renderer)
	, m_allocator(allocator)
	, m_fragment_shader_source(m_allocator)
	, m_vertex_shader_source(m_allocator)
	, m_attributes(m_allocator)
	, m_passes(m_allocator)
	, m_pass_hashes(m_allocator)
	, m_combinations(m_allocator)
	, m_default_combination(m_allocator)
{
	m_default_combination.m_program_id = 0;
	m_default_combination.m_pass_hash = 0;
	m_default_combination.m_hash = 0;
	m_default_combination.m_vertex_id = 0;
	m_default_combination.m_fragment_id = 0;
}


Shader::~Shader()
{
	for(int i = 0; i < m_combinations.size(); ++i)
	{	
		glDeleteProgram(m_combinations[i]->m_program_id);
		glDeleteShader(m_combinations[i]->m_vertex_id);
		glDeleteShader(m_combinations[i]->m_fragment_id);
		m_allocator.deleteObject(m_combinations[i]);
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


Shader::Combination* Shader::getCombination(uint32_t hash, uint32_t pass_hash)
{
	for(int i = 0, c = m_combinations.size(); i < c; ++i)
	{
		if(m_combinations[i]->m_hash == hash && m_combinations[i]->m_pass_hash == pass_hash)
		{
			return m_combinations[i];
		}
	}
	return &m_default_combination;
}


bool Shader::hasPass(uint32_t pass_hash)
{
	for(int i = 0, c = m_pass_hashes.size(); i < c; ++i)
	{
		if(m_pass_hashes[i] == pass_hash)
			return true;
	}
	return false;
}


void Shader::createCombination(const char* defines)
{
	ASSERT(isReady());
	for(int pass_idx = 0; pass_idx < m_passes.size(); ++pass_idx)
	{
		uint32_t hash = defines[0] == '\0' ? 0 : crc32(defines);
		uint32_t pass_hash = crc32(m_passes[pass_idx].c_str());

		Shader::Combination* combination = getCombination(hash, pass_hash);
		if(!combination || combination == &m_default_combination)
		{
			Combination* combination = m_allocator.newObject<Combination>(m_allocator);
			m_combinations.push(combination);
			combination->m_defines = defines;
			combination->m_program_id = glCreateProgram();
			combination->m_hash = hash;
			combination->m_pass_hash = pass_hash;

			char version_str[20];
			copyString(version_str, sizeof(version_str), m_renderer.getGLSLVersion() >= 330 ? "#version 330\n" : "#version 130\n");

			char pass_str[1024];
			copyString(pass_str, sizeof(pass_str), "#define ");
			catCString(pass_str, sizeof(pass_str), m_passes[pass_idx].c_str());
			catCString(pass_str, sizeof(pass_str), "_PASS\n");

			combination->m_vertex_id = glCreateShader(GL_VERTEX_SHADER);
			const GLchar* vs_strings[] = { version_str, pass_str, defines, m_vertex_shader_source.c_str() };
			glShaderSource(combination->m_vertex_id, sizeof(vs_strings) / sizeof(vs_strings[0]), vs_strings, NULL);
			glCompileShader(combination->m_vertex_id);
			glAttachShader(combination->m_program_id, combination->m_vertex_id);

			combination->m_fragment_id = glCreateShader(GL_FRAGMENT_SHADER);
			const GLchar* fs_strings[] = { version_str, pass_str, defines, m_fragment_shader_source.c_str() };
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
		
			for (int i = 0; i < sizeof(combination->m_vertex_attributes_ids) / sizeof(combination->m_vertex_attributes_ids[0]); ++i)
			{
				combination->m_vertex_attributes_ids[i] = -1;
			}

			for (int i = 0; i < m_attributes.size(); ++i)
			{
				int attr_idx = m_renderer.getAttributeNameIndex(m_attributes[i].c_str());
				combination->m_vertex_attributes_ids[attr_idx] = glGetAttribLocation(combination->m_program_id, m_attributes[i].c_str());
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
			combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::SHADOW_MATRIX0] = glGetUniformLocation(combination->m_program_id, "shadowmap_matrix0");
			combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::SHADOW_MATRIX1] = glGetUniformLocation(combination->m_program_id, "shadowmap_matrix1");
			combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::SHADOW_MATRIX2] = glGetUniformLocation(combination->m_program_id, "shadowmap_matrix2");
			combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::SHADOW_MATRIX3] = glGetUniformLocation(combination->m_program_id, "shadowmap_matrix3");
			combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::LIGHT_POSITION] = glGetUniformLocation(combination->m_program_id, "light_pos");
			combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::LIGHT_RANGE] = glGetUniformLocation(combination->m_program_id, "light_range");
			combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::LIGHT_FOV] = glGetUniformLocation(combination->m_program_id, "light_fov");
			combination->m_fixed_cached_uniforms[(int)FixedCachedUniforms::LIGHT_DIR] = glGetUniformLocation(combination->m_program_id, "light_dir");
		}
	}
}


void Shader::parseAttributes(lua_State* L)
{
	lua_getglobal(L, "attributes");
	int len = (int)lua_rawlen(L, -1);
	for (int i = 0; i < len; ++i)
	{
		if (lua_rawgeti(L, -1, 1 + i) == LUA_TSTRING)
		{
			m_attributes.push(Lumix::string(lua_tostring(L, -1), m_allocator));
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}


void Shader::parsePasses(lua_State* L)
{
	if (lua_getglobal(L, "passes") == LUA_TTABLE)
	{
		int len = (int)lua_rawlen(L, -1);
		for (int i = 0; i < len; ++i)
		{
			if (lua_rawgeti(L, -1, 1 + i) == LUA_TSTRING)
			{
				m_passes.push(Lumix::string(lua_tostring(L, -1), m_allocator));
				m_pass_hashes.push(crc32(m_passes.back().c_str()));
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}


void Shader::parseSourceCode(lua_State* L)
{
	if (lua_getglobal(L, "vertex_shader") == LUA_TSTRING)
	{
		m_vertex_shader_source = lua_tostring(L, -1);
	}
	if (lua_getglobal(L, "fragment_shader") == LUA_TSTRING)
	{
		m_fragment_shader_source = lua_tostring(L, -1);
	}
	lua_pop(L, 2);
}


void Shader::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if(success)
	{
		lua_State* L = luaL_newstate();
		
		bool errors = luaL_loadbuffer(L, (const char*)file->getBuffer(), file->size(), "") != LUA_OK;
		errors = errors || lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK;
		if (errors)
		{
			g_log_error.log("lua") << getPath().c_str() << ": " << lua_tostring(L, -1);
			onFailure();
		}
		else
		{
			parseSourceCode(L);
			parsePasses(L);
			parseAttributes(L);

			m_size = file->size();
			decrementDepCount();

			if (!m_combinations.empty())
			{
				Array<Combination*> old_combinations(m_allocator);
				for (int i = 0; i < m_combinations.size(); ++i)
				{
					old_combinations.push(m_combinations[i]);
				}
				m_combinations.clear();
				for (int i = 0; i < old_combinations.size(); ++i)
				{
					createCombination(old_combinations[i]->m_defines.c_str());
				}
				for (int i = 0; i < old_combinations.size(); ++i)
				{
					m_allocator.deleteObject(old_combinations[i]);
				}
			}
			else
			{
				createCombination("");
			}
		}
		lua_close(L);
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


} // ~namespace Lumix
