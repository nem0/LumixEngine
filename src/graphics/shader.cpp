#include "graphics/shader.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec3.h"
#include "graphics/renderer.h"
#include "graphics/shader_manager.h"
#include <bgfx.h>
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


Shader::Shader(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_instances(m_allocator)
	, m_texture_slot_count(0)
{
}


Shader::~Shader()
{
	doUnload();
}


uint32_t Shader::getDefineMask(const char* define) const
{
	int c = lengthOf(m_combintions.m_defines);
	for (int i = 0; i < c; ++i)
	{
		if (strcmp(define, m_combintions.m_defines[i]) == 0)
		{
			return 1 << i;
		}
	}
	return 0;
}


ShaderInstance& Shader::getInstance(uint32_t mask)
{
	for (int i = 0; i < m_instances.size(); ++i)
	{
		if (m_instances[i]->m_combination == mask)
		{
			return *m_instances[i];
		}
	}

	g_log_error.log("Shader") << "Unknown shader combination requested: " << mask;
	return *m_instances[0];
}


void ShaderCombinations::parsePasses(lua_State* L)
{
	if (lua_getglobal(L, "passes") == LUA_TTABLE)
	{
		int len = (int)lua_rawlen(L, -1);
		for (int i = 0; i < len; ++i)
		{
			if (lua_rawgeti(L, -1, 1 + i) == LUA_TSTRING)
			{
				copyString(m_passes[i], sizeof(m_passes[i]), lua_tostring(L, -1));
			}
			lua_pop(L, 1);
		}
		m_pass_count = len;
	}
	lua_pop(L, 1);
}


void Shader::parseTextureSlots(lua_State* L)
{
	for (int i = 0; i < m_texture_slot_count; ++i)
	{
		m_texture_slots[i].reset();
	}
	if (lua_getglobal(L, "texture_slots") == LUA_TTABLE)
	{
		m_texture_slot_count = (int)lua_rawlen(L, -1);
		for (int i = 0; i < m_texture_slot_count; ++i)
		{
			if (lua_rawgeti(L, -1, 1 + i) == LUA_TTABLE)
			{
				if (lua_getfield(L, -1, "name") == LUA_TSTRING)
				{
					copyString(m_texture_slots[i].m_name, sizeof(m_texture_slots[i].m_name), lua_tostring(L, -1));
				}
				lua_pop(L, 1);
				if (lua_getfield(L, -1, "uniform") == LUA_TSTRING)
				{
					copyString(m_texture_slots[i].m_uniform, sizeof(m_texture_slots[i].m_uniform), lua_tostring(L, -1));
					m_texture_slots[i].m_uniform_handle = bgfx::createUniform(m_texture_slots[i].m_uniform, bgfx::UniformType::Int1);
					m_texture_slots[i].m_uniform_hash = crc32(m_texture_slots[i].m_uniform);
				}
				lua_pop(L, 1);
				if (lua_getfield(L, -1, "define") == LUA_TSTRING)
				{
					copyString(m_texture_slots[i].m_define, sizeof(m_texture_slots[i].m_define), lua_tostring(L, -1));
				}
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}


bgfx::ProgramHandle Shader::createProgram(int pass_idx, int mask) const
{
	char shader_name[LUMIX_MAX_PATH];
	PathUtils::getBasename(shader_name, sizeof(shader_name), getPath().c_str());

	const char* pass = m_combintions.m_passes[pass_idx];
	char path[LUMIX_MAX_PATH];
	copyString(path, sizeof(path), "shaders/compiled/");
	catCString(path, sizeof(path), shader_name);
	catCString(path, sizeof(path), "_");
	catCString(path, sizeof(path), pass);
	char mask_str[10];
	int actual_mask = mask & m_combintions.m_vs_combinations[pass_idx];
	toCString(actual_mask, mask_str, sizeof(mask_str));
	catCString(path, sizeof(path), mask_str);
	catCString(path, sizeof(path), "_vs.shb");
	
	auto fp = fopen(path, "rb");
	if (!fp)
	{
		return BGFX_INVALID_HANDLE;
	}
	fseek(fp, 0, SEEK_END);
	auto s = ftell(fp);
	auto* mem = bgfx::alloc(s + 1);
	fseek(fp, 0, SEEK_SET);
	fread(mem->data, s, 1, fp);
	mem->data[s] = '\0';
	auto vs = bgfx::createShader(mem);
	fclose(fp);

	copyString(path, sizeof(path), "shaders/compiled/");
	catCString(path, sizeof(path), shader_name);
	catCString(path, sizeof(path), "_");
	catCString(path, sizeof(path), pass);
	actual_mask = mask & m_combintions.m_fs_combinations[pass_idx];
	toCString(actual_mask, mask_str, sizeof(mask_str));
	catCString(path, sizeof(path), mask_str);
	catCString(path, sizeof(path), "_fs.shb");

	fp = fopen(path, "rb");
	if (!fp)
	{
		return BGFX_INVALID_HANDLE;
	}
	fseek(fp, 0, SEEK_END);
	s = ftell(fp);
	mem = bgfx::alloc(s + 1);
	fseek(fp, 0, SEEK_SET);
	fread(mem->data, s, 1, fp);
	mem->data[s] = '\0';
	auto ps = bgfx::createShader(mem);
	fclose(fp);

	return bgfx::createProgram(vs, ps, false);
}


Renderer& Shader::getRenderer()
{
	return static_cast<ShaderManager*>(m_resource_manager.get(ResourceManager::SHADER))->getRenderer();
}


bool Shader::generateInstances()
{

	for (int i = 0; i < m_instances.size(); ++i)
	{
		m_allocator.deleteObject(m_instances[i]);
	}
	m_instances.clear();

	int count = 1 << m_combintions.m_define_count;
	Renderer& renderer = getRenderer();
	for (int mask = 0; mask < count; ++mask)
	{
		ShaderInstance* instance = m_allocator.newObject<ShaderInstance>(m_allocator);

		instance->m_combination = mask;

		for (int pass_idx = 0; pass_idx < m_combintions.m_pass_count; ++pass_idx)
		{
			int global_idx = renderer.getPassIdx(m_combintions.m_passes[pass_idx]);
			instance->m_program_handles[global_idx] = createProgram(pass_idx, mask);
			if (!bgfx::isValid(instance->m_program_handles[global_idx]))
			{
				m_allocator.deleteObject(instance);
				return false;
			}
		}
		m_instances.push(instance);
	}
	return true;
}


void ShaderCombinations::parse(lua_State* L)
{
	parsePasses(L);

	parseCombinations(L, "fs_combinations", m_fs_combinations);
	parseCombinations(L, "vs_combinations", m_vs_combinations);
}


void Shader::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if(success)
	{
		lua_State* L = luaL_newstate();
		luaL_openlibs(L);
		
		bool errors = luaL_loadbuffer(L, (const char*)file->getBuffer(), file->size(), "") != LUA_OK;
		errors = errors || lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK;
		if (errors)
		{
			g_log_error.log("lua") << getPath().c_str() << ": " << lua_tostring(L, -1);
			onFailure();
		}
		else
		{
			parseTextureSlots(L);
			m_combintions.parse(L);
			if (!generateInstances())
			{
				g_log_error.log("renderer") << "Could not load instances of shader " << m_path.c_str();
				onFailure();
			}
			else
			{
				m_size = file->size();
				decrementDepCount();
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
	for (int i = 0; i < m_instances.size(); ++i)
	{
		m_allocator.deleteObject(m_instances[i]);
	}
	m_instances.clear();
	m_size = 0;
	onEmpty();
}


ShaderInstance::~ShaderInstance()
{
	for (int i = 0; i < lengthOf(m_program_handles); ++i)
	{
		if (bgfx::isValid(m_program_handles[i]))
		{
			bgfx::destroyProgram(m_program_handles[i]);
		}
	}
}


static int indexOf(const ShaderCombinations::Passes& passes, const char* pass)
{
	for (int i = 0; i < lengthOf(passes); ++i)
	{
		if (strcmp(passes[i], pass) == 0)
		{
			return i;
		}
	}
	return 0;
}


static int indexOf(ShaderCombinations& combination, const char* define)
{
	for (int i = 0; i < combination.m_define_count; ++i)
	{
		if (strcmp(combination.m_defines[i], define) == 0)
		{
			return i;
		}
	}

	copyString(combination.m_defines[combination.m_define_count], sizeof(combination.m_defines[0]), define);
	++combination.m_define_count;
	return combination.m_define_count - 1;
}


void ShaderCombinations::parseCombinations(lua_State* L, const char* name, int* output)
{
	if (lua_getglobal(L, name) == LUA_TTABLE)
	{
		int len = (int)lua_rawlen(L, -1);
		for (int pass_idx = 0; pass_idx < len; ++pass_idx)
		{
			int& define_mask = output[pass_idx];
			if (lua_rawgeti(L, -1, 1 + pass_idx) == LUA_TTABLE)
			{
				int len = (int)lua_rawlen(L, -1);
				for (int i = 0; i < len; ++i)
				{
					if (lua_rawgeti(L, -1, 1 + i) == LUA_TSTRING)
					{
						const char* tmp = lua_tostring(L, -1);
						define_mask |= 1 << indexOf(*this, tmp);
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}


bool Shader::getShaderCombinations(const char* shader_content, ShaderCombinations* output)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	bool errors = luaL_loadbuffer(L, shader_content, strlen(shader_content), "") != LUA_OK;
	errors = errors || lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK;
	if (errors)
	{
		return false;
	}
	else
	{
		output->parse(L);
	}
	lua_close(L);
	return true;
}


} // ~namespace Lumix
