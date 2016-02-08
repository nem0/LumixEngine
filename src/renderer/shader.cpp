#include "renderer/shader.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "renderer/renderer.h"
#include "renderer/shader_manager.h"
#include <bgfx/bgfx.h>
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
	ASSERT(isEmpty());
}


bool Shader::hasDefine(uint8 define_idx) const
{
	return (m_combintions.m_all_defines_mask & (1 << define_idx)) != 0;
}


ShaderInstance* Shader::getFirstInstance()
{
	return m_instances.empty() ? nullptr : m_instances[0];
}


ShaderInstance& Shader::getInstance(uint32 mask)
{
	for (int i = 0; i < m_instances.size(); ++i)
	{
		if (m_instances[i]->m_define_mask == mask)
		{
			return *m_instances[i];
		}
	}

	g_log_error.log("Shader") << "Unknown shader combination requested: " << mask;
	return *m_instances[0];
}


ShaderCombinations::ShaderCombinations()
{
	setMemory(this, 0, sizeof(*this));
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
					copyString(m_texture_slots[i].m_name,
						sizeof(m_texture_slots[i].m_name),
						lua_tostring(L, -1));
				}
				lua_pop(L, 1);
				if (lua_getfield(L, -1, "is_atlas") == LUA_TBOOLEAN)
				{
					m_texture_slots[i].m_is_atlas = lua_toboolean(L, -1) != 0;
				}
				lua_pop(L, 1);
				if (lua_getfield(L, -1, "uniform") == LUA_TSTRING)
				{
					copyString(m_texture_slots[i].m_uniform,
						sizeof(m_texture_slots[i].m_uniform),
						lua_tostring(L, -1));
					m_texture_slots[i].m_uniform_handle =
						bgfx::createUniform(m_texture_slots[i].m_uniform, bgfx::UniformType::Int1);
					m_texture_slots[i].m_uniform_hash = crc32(m_texture_slots[i].m_uniform);
				}
				lua_pop(L, 1);
				if (lua_getfield(L, -1, "define") == LUA_TSTRING)
				{
					m_texture_slots[i].m_define_idx =
						getRenderer().getShaderDefineIdx(lua_tostring(L, -1));
				}
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}


Renderer& Shader::getRenderer()
{
	auto* manager = m_resource_manager.get(ResourceManager::SHADER);
	return static_cast<ShaderManager*>(manager)->getRenderer();
}


uint32 Shader::getDefineMaskFromDense(uint32 dense) const
{
	uint32 mask = 0;
	for (int i = 0; i < sizeof(dense) * 8; ++i)
	{
		if (dense & (1 << i))
		{
			mask |= 1 << m_combintions.m_defines[i];
		}
	}
	return mask;
}


bool Shader::generateInstances()
{
	for (int i = 0; i < m_instances.size(); ++i)
	{
		LUMIX_DELETE(m_allocator, m_instances[i]);
	}
	m_instances.clear();

	uint32 count = 1 << m_combintions.m_define_count;

	auto* binary_manager = m_resource_manager.get(ResourceManager::SHADER_BINARY);
	char basename[MAX_PATH_LENGTH];
	PathUtils::getBasename(basename, sizeof(basename), getPath().c_str());

	for (uint32 mask = 0; mask < count; ++mask)
	{
		ShaderInstance* instance = LUMIX_NEW(m_allocator, ShaderInstance)(*this);
		m_instances.push(instance);

		instance->m_define_mask = getDefineMaskFromDense(mask);

		for (int pass_idx = 0; pass_idx < m_combintions.m_pass_count; ++pass_idx)
		{
			const char* pass = m_combintions.m_passes[pass_idx];
			char path[MAX_PATH_LENGTH];
			copyString(path, "shaders/compiled/");
			catString(path, basename);
			catString(path, "_");
			catString(path, pass);
			char mask_str[10];
			int actual_mask = mask & m_combintions.m_vs_local_mask[pass_idx];
			toCString(actual_mask, mask_str, sizeof(mask_str));
			catString(path, mask_str);
			catString(path, "_vs.shb");

			Path vs_path(path);
			auto* vs_binary = static_cast<ShaderBinary*>(binary_manager->load(vs_path));
			addDependency(*vs_binary);
			instance->m_binaries[pass_idx * 2] = vs_binary;

			copyString(path, "shaders/compiled/");
			catString(path, basename);
			catString(path, "_");
			catString(path, pass);
			actual_mask = mask & m_combintions.m_fs_local_mask[pass_idx];
			toCString(actual_mask, mask_str, sizeof(mask_str));
			catString(path, mask_str);
			catString(path, "_fs.shb");

			Path fs_path(path);
			auto* fs_binary = static_cast<ShaderBinary*>(binary_manager->load(fs_path));
			addDependency(*fs_binary);
			instance->m_binaries[pass_idx * 2 + 1] = fs_binary;
		}
	}
	return true;
}


void ShaderCombinations::parse(Renderer& renderer, lua_State* L)
{
	parsePasses(L);

	m_all_defines_mask = 0;
	parseCombinations(renderer, L, "fs_combinations", m_fs_local_mask);
	parseCombinations(renderer, L, "vs_combinations", m_vs_local_mask);
}


bool Shader::load(FS::IFile& file)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	bool errors = luaL_loadbuffer(L, (const char*)file.getBuffer(), file.size(), "") != LUA_OK;
	errors = errors || lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK;
	if (errors)
	{
		g_log_error.log("lua") << getPath().c_str() << ": " << lua_tostring(L, -1);
		lua_pop(L, 1);
		return false;
	}

	parseTextureSlots(L);
	m_combintions.parse(getRenderer(), L);
	if (!generateInstances())
	{
		g_log_error.log("renderer") << "Could not load instances of shader " << getPath().c_str();
		return false;
	}

	m_size = file.size();
	lua_close(L);
	return true;
}


void Shader::onBeforeReady()
{
	for (auto* instance : m_instances)
	{
		auto** binaries = instance->m_binaries;
		for (int i = 0; i < Lumix::lengthOf(instance->m_binaries); i += 2)
		{
			if (!binaries[i] || !binaries[i + 1]) continue;

			auto vs_handle = binaries[i]->getHandle();
			auto fs_handle = binaries[i + 1]->getHandle();
			auto program = bgfx::createProgram(vs_handle, fs_handle);
			
			ASSERT(bgfx::isValid(program));

			int pass_idx = i / 2;
			int global_idx = getRenderer().getPassIdx(m_combintions.m_passes[pass_idx]);

			instance->m_program_handles[global_idx] = program;
		}
	}
}


void Shader::unload(void)
{
	for (int i = 0; i < m_texture_slot_count; ++i)
	{
		if (bgfx::isValid(m_texture_slots[i].m_uniform_handle))
		{
			bgfx::destroyUniform(m_texture_slots[i].m_uniform_handle);
		}
		m_texture_slots[i].m_uniform_handle = BGFX_INVALID_HANDLE;
	}
	m_texture_slot_count = 0;

	for (int i = 0; i < m_instances.size(); ++i)
	{
		LUMIX_DELETE(m_allocator, m_instances[i]);
	}
	m_instances.clear();
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

	for (auto* binary : m_binaries)
	{
		if (!binary) continue;

		m_shader.removeDependency(*binary);
		auto* manager = binary->getResourceManager().get(ResourceManager::SHADER_BINARY);
		manager->unload(*binary);
	}
}


static int indexOf(ShaderCombinations& combination, uint8 define_idx)
{
	for (int i = 0; i < combination.m_define_count; ++i)
	{
		if (combination.m_defines[i] == define_idx)
		{
			return i;
		}
	}

	combination.m_defines[combination.m_define_count] = define_idx;
	++combination.m_define_count;
	return combination.m_define_count - 1;
}


void ShaderCombinations::parseCombinations(Renderer& renderer,
	lua_State* L,
	const char* name,
	int* output)
{
	if (lua_getglobal(L, name) == LUA_TTABLE)
	{
		int len = (int)lua_rawlen(L, -1);
		for (int pass_idx = 0; pass_idx < len; ++pass_idx)
		{
			if (lua_rawgeti(L, -1, 1 + pass_idx) == LUA_TTABLE)
			{
				int len = (int)lua_rawlen(L, -1);
				for (int i = 0; i < len; ++i)
				{
					if (lua_rawgeti(L, -1, 1 + i) == LUA_TSTRING)
					{
						const char* tmp = lua_tostring(L, -1);
						int define_idx = renderer.getShaderDefineIdx(tmp);
						m_all_defines_mask |= 1 << define_idx;
						output[pass_idx] |= 1 << indexOf(*this, define_idx);
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}


bool Shader::getShaderCombinations(Renderer& renderer,
	const char* shader_content,
	ShaderCombinations* output)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	bool errors = luaL_loadbuffer(L, shader_content, stringLength(shader_content), "") != LUA_OK;
	errors = errors || lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK;
	if (errors)
	{
		g_log_error.log("lua") << lua_tostring(L, -1);
		lua_pop(L, 1);
		return false;
	}
	else
	{
		output->parse(renderer, L);
	}
	lua_close(L);
	return true;
}


ShaderBinary::ShaderBinary(const Path& path,
	ResourceManager& resource_manager,
	IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_handle(BGFX_INVALID_HANDLE)
{
}


void ShaderBinary::unload()
{
	if (bgfx::isValid(m_handle)) bgfx::destroyShader(m_handle);
	m_handle = BGFX_INVALID_HANDLE;
}


bool ShaderBinary::load(FS::IFile& file)
{
	auto* mem = bgfx::alloc((uint32)file.size() + 1);
	file.read(mem->data, file.size());
	mem->data[file.size()] = '\0';
	m_handle = bgfx::createShader(mem);
	m_size = file.size();
	return bgfx::isValid(m_handle);
}


} // namespace Lumix
