#include "renderer/shader.h"
#include "engine/core/crc32.h"
#include "engine/core/fs/file_system.h"
#include "engine/core/lua_wrapper.h"
#include "engine/core/log.h"
#include "engine/core/path_utils.h"
#include "engine/core/resource_manager.h"
#include "engine/core/resource_manager_base.h"
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
	, m_uniforms(m_allocator)
	, m_render_states(0)
	, m_all_defines_mask(0)
{
}


Shader::~Shader()
{
	ASSERT(isEmpty());
}


bool Shader::hasDefine(uint8 define_idx) const
{
	return (m_combintions.all_defines_mask & (1 << define_idx)) != 0;
}


ShaderInstance& Shader::getInstance(uint32 mask)
{
	mask = mask & m_all_defines_mask;
	for (int i = 0; i < m_instances.size(); ++i)
	{
		if (m_instances[i]->define_mask == mask)
		{
			return *m_instances[i];
		}
	}

	g_log_error.log("Renderer") << "Unknown shader combination requested: " << mask;
	return *m_instances[0];
}


ShaderCombinations::ShaderCombinations()
{
	setMemory(this, 0, sizeof(*this));
}


Renderer& Shader::getRenderer()
{
	auto* manager = m_resource_manager.get(ResourceManager::SHADER);
	return static_cast<ShaderManager*>(manager)->getRenderer();
}


static uint32 getDefineMaskFromDense(const Shader& shader, uint32 dense)
{
	uint32 mask = 0;
	for (int i = 0; i < sizeof(dense) * 8; ++i)
	{
		if (dense & (1 << i))
		{
			mask |= 1 << shader.m_combintions.defines[i];
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

	uint32 count = 1 << m_combintions.define_count;

	auto* binary_manager = m_resource_manager.get(ResourceManager::SHADER_BINARY);
	char basename[MAX_PATH_LENGTH];
	PathUtils::getBasename(basename, sizeof(basename), getPath().c_str());

	for (uint32 mask = 0; mask < count; ++mask)
	{
		ShaderInstance* instance = LUMIX_NEW(m_allocator, ShaderInstance)(*this);
		m_instances.push(instance);

		instance->define_mask = getDefineMaskFromDense(*this, mask);
		m_all_defines_mask |= instance->define_mask;

		for (int pass_idx = 0; pass_idx < m_combintions.pass_count; ++pass_idx)
		{
			const char* pass = m_combintions.passes[pass_idx];
			StaticString<MAX_PATH_LENGTH> path("shaders/compiled/");
			int actual_mask = mask & m_combintions.vs_local_mask[pass_idx];
			path << basename << "_" << pass << actual_mask << "_vs.shb";

			Path vs_path(path);
			auto* vs_binary = static_cast<ShaderBinary*>(binary_manager->load(vs_path));
			addDependency(*vs_binary);
			instance->binaries[pass_idx * 2] = vs_binary;

			path.data[0] = '\0';
			actual_mask = mask & m_combintions.fs_local_mask[pass_idx];
			path << "shaders/compiled/" << basename << "_" << pass << actual_mask << "_fs.shb";

			Path fs_path(path);
			auto* fs_binary = static_cast<ShaderBinary*>(binary_manager->load(fs_path));
			addDependency(*fs_binary);
			instance->binaries[pass_idx * 2 + 1] = fs_binary;
		}
	}
	return true;
}


static void registerCFunction(lua_State* L, const char* name, lua_CFunction function)
{
	lua_pushcfunction(L, function);
	lua_setglobal(L, name);
}


static ShaderCombinations* getCombinations(lua_State* L)
{
	ShaderCombinations* ret = nullptr;
	if (lua_getglobal(L, "this") == LUA_TLIGHTUSERDATA)
	{
		ret = LuaWrapper::toType<ShaderCombinations*>(L, -1);
	}
	lua_pop(L, 1);
	return ret;
}


static Shader* getShader(lua_State* L)
{
	Shader* ret = nullptr;
	if (lua_getglobal(L, "shader") == LUA_TLIGHTUSERDATA)
	{
		ret = LuaWrapper::toType<Shader*>(L, -1);
	}
	lua_pop(L, 1);
	return ret;
}


static void texture_slot(lua_State* state, const char* name, const char* uniform)
{
	auto* shader = getShader(state);
	if (!shader) return;
	auto& slot = shader->m_texture_slots[shader->m_texture_slot_count];
	copyString(slot.name, name);
	slot.uniform_handle = bgfx::createUniform(uniform, bgfx::UniformType::Int1);
	copyString(slot.uniform, uniform);
	++shader->m_texture_slot_count;
}


static void atlas(lua_State* L)
{
	auto* shader = getShader(L);
	if (!shader) return;
	shader->m_texture_slots[shader->m_texture_slot_count - 1].is_atlas = true;
}


static void texture_define(lua_State* L, const char* define)
{
	auto* shader = getShader(L);
	if (!shader) return;
	Renderer* renderer = nullptr;
	if (lua_getglobal(L, "renderer") == LUA_TLIGHTUSERDATA)
	{
		renderer = LuaWrapper::toType<Renderer*>(L, -1);
	}
	lua_pop(L, 1);
	auto& slot = shader->m_texture_slots[shader->m_texture_slot_count - 1];
	slot.define_idx = renderer->getShaderDefineIdx(lua_tostring(L, -1));
}


static void uniform(lua_State* L, const char* name, const char* type)
{
	auto* shader = getShader(L);
	if (!shader) return;
	auto& u = shader->m_uniforms.emplace();
	copyString(u.name, name);
	u.name_hash = crc32(name);
	if (compareString(type, "float") == 0)
	{
		u.type = Shader::Uniform::FLOAT;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (compareString(type, "color") == 0)
	{
		u.type = Shader::Uniform::COLOR;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (compareString(type, "int") == 0)
	{
		u.type = Shader::Uniform::INT;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Int1);
	}
	else if (compareString(type, "matrix4") == 0)
	{
		u.type = Shader::Uniform::MATRIX4;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Mat4);
	}
	else if (compareString(type, "time") == 0)
	{
		u.type = Shader::Uniform::TIME;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (compareString(type, "vec3") == 0)
	{
		u.type = Shader::Uniform::VEC3;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else 
	{
		g_log_error.log("Renderer") << "Unknown uniform type " << type << " in " << shader->getPath().c_str();
	}
}


static void pass(lua_State* state, const char* name)
{
	auto* cmb = getCombinations(state);
	copyString(cmb->passes[cmb->pass_count].data, name);
	cmb->vs_local_mask[cmb->pass_count] = 0;
	cmb->fs_local_mask[cmb->pass_count] = 0;
	++cmb->pass_count;
}


static int indexOf(ShaderCombinations& combination, uint8 define_idx)
{
	for (int i = 0; i < combination.define_count; ++i)
	{
		if (combination.defines[i] == define_idx)
		{
			return i;
		}
	}

	combination.defines[combination.define_count] = define_idx;
	++combination.define_count;
	return combination.define_count - 1;
}


static void alpha_blending(lua_State* L, const char* mode)
{
	Shader* shader = getShader(L);
	if (!shader) return;
	if (compareString(mode, "add") == 0)
	{
		shader->m_render_states |= BGFX_STATE_BLEND_ADD;
	}
	else if (compareString(mode, "alpha") == 0)
	{
		shader->m_render_states |= BGFX_STATE_BLEND_ALPHA;
	}
	else
	{
		g_log_error.log("Renderer") << "Uknown blend mode " << mode << " in " << shader->getPath().c_str();
	}
}


static void backface_culling(lua_State* L, bool enabled)
{
	Shader* shader = getShader(L);
	if (!shader) return;
	if (enabled)
	{
		shader->m_render_states |= BGFX_STATE_CULL_CW;
	}
	else
	{
		shader->m_render_states &= ~BGFX_STATE_CULL_MASK;
	}
}


static void depth_test(lua_State* L, bool enabled)
{
	Shader* shader = nullptr;
	if (lua_getglobal(L, "shader") == LUA_TLIGHTUSERDATA)
	{
		shader = LuaWrapper::toType<Shader*>(L, -1);
	}
	lua_pop(L, 1);
	if (!shader) return;
	if (enabled)
	{
		shader->m_render_states |= BGFX_STATE_DEPTH_TEST_LEQUAL;
	}
	else
	{
		shader->m_render_states &= ~BGFX_STATE_DEPTH_TEST_MASK;
	}
}


static void fs(lua_State* L)
{
	auto* cmb = getCombinations(L);
	Renderer* renderer = nullptr;
	if (lua_getglobal(L, "renderer") == LUA_TLIGHTUSERDATA)
	{
		renderer = LuaWrapper::toType<Renderer*>(L, -1);
	}
	lua_pop(L, 1);

	LuaWrapper::checkTableArg(L, 1);
	int len = (int)lua_rawlen(L, 1);
	for (int i = 0; i < len; ++i)
	{
		if (lua_rawgeti(L, 1, i + 1) == LUA_TSTRING)
		{
			const char* tmp = lua_tostring(L, -1);
			int define_idx = renderer->getShaderDefineIdx(tmp);
			cmb->all_defines_mask |= 1 << define_idx;
			cmb->fs_local_mask[cmb->pass_count - 1] |= 1 << indexOf(*cmb, define_idx);
		}
	}
}


static void vs(lua_State* L)
{
	auto* cmb = getCombinations(L);
	Renderer* renderer = nullptr;
	if (lua_getglobal(L, "renderer") == LUA_TLIGHTUSERDATA)
	{
		renderer = LuaWrapper::toType<Renderer*>(L, -1);
	}
	lua_pop(L, 1);

	LuaWrapper::checkTableArg(L, 1);
	int len = (int)lua_rawlen(L, 1);
	for (int i = 0; i < len; ++i)
	{
		if (lua_rawgeti(L, -1, i + 1) == LUA_TSTRING)
		{
			const char* tmp = lua_tostring(L, -1);
			int define_idx = renderer->getShaderDefineIdx(tmp);
			cmb->all_defines_mask |= 1 << define_idx;
			cmb->vs_local_mask[cmb->pass_count - 1] |= 1 << indexOf(*cmb, define_idx);
		}
	}
}


static void registerFunctions(Shader* shader, ShaderCombinations* combinations, Renderer* renderer, lua_State* L)
{
	lua_pushlightuserdata(L, combinations);
	lua_setglobal(L, "this");
	lua_pushlightuserdata(L, renderer);
	lua_setglobal(L, "renderer");
	lua_pushlightuserdata(L, shader);
	lua_setglobal(L, "shader");
	registerCFunction(L, "pass", &LuaWrapper::wrap<decltype(&pass), pass>);
	registerCFunction(L, "fs", &LuaWrapper::wrap<decltype(&fs), fs>);
	registerCFunction(L, "vs", &LuaWrapper::wrap<decltype(&vs), vs>);
	registerCFunction(L, "backface_culling", &LuaWrapper::wrap<decltype(&backface_culling), backface_culling>);
	registerCFunction(L, "depth_test", &LuaWrapper::wrap<decltype(&depth_test), depth_test>);
	registerCFunction(L, "alpha_blending", &LuaWrapper::wrap<decltype(&alpha_blending), alpha_blending>);
	registerCFunction(L, "texture_slot", &LuaWrapper::wrap<decltype(&texture_slot), texture_slot>);
	registerCFunction(L, "texture_define", &LuaWrapper::wrap<decltype(&texture_define), texture_define>);
	registerCFunction(L, "atlas", &LuaWrapper::wrap<decltype(&atlas), atlas>);
	registerCFunction(L, "uniform", &LuaWrapper::wrap<decltype(&uniform), uniform>);
}


bool Shader::load(FS::IFile& file)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	registerFunctions(this, &m_combintions, &getRenderer(), L);
	m_render_states = BGFX_STATE_CULL_CW | BGFX_STATE_DEPTH_TEST_LEQUAL;

	bool errors = luaL_loadbuffer(L, (const char*)file.getBuffer(), file.size(), "") != LUA_OK;
	errors = errors || lua_pcall(L, 0, 0, 0) != LUA_OK;
	if (errors)
	{
		g_log_error.log("Renderer") << getPath().c_str() << ": " << lua_tostring(L, -1);
		lua_pop(L, 1);
		return false;
	}
	
	if (!generateInstances())
	{
		g_log_error.log("Renderer") << "Could not load instances of shader " << getPath().c_str();
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
		auto** binaries = instance->binaries;
		for (int i = 0; i < Lumix::lengthOf(instance->binaries); i += 2)
		{
			if (!binaries[i] || !binaries[i + 1]) continue;

			auto vs_handle = binaries[i]->getHandle();
			auto fs_handle = binaries[i + 1]->getHandle();
			auto program = bgfx::createProgram(vs_handle, fs_handle);

			ASSERT(bgfx::isValid(program));

			int pass_idx = i / 2;
			int global_idx = getRenderer().getPassIdx(m_combintions.passes[pass_idx]);

			instance->program_handles[global_idx] = program;
		}
	}
}


void Shader::unload(void)
{
	m_combintions = {};

	for (auto& uniform : m_uniforms)
	{
		bgfx::destroyUniform(uniform.handle);
	}
	m_uniforms.clear();

	for (int i = 0; i < m_texture_slot_count; ++i)
	{
		if (bgfx::isValid(m_texture_slots[i].uniform_handle))
		{
			bgfx::destroyUniform(m_texture_slots[i].uniform_handle);
		}
		m_texture_slots[i].uniform_handle = BGFX_INVALID_HANDLE;
	}
	m_texture_slot_count = 0;

	for (auto* i : m_instances)
	{
		LUMIX_DELETE(m_allocator, i);
	}
	m_instances.clear();
}


ShaderInstance::~ShaderInstance()
{
	for (int i = 0; i < lengthOf(program_handles); ++i)
	{
		if (bgfx::isValid(program_handles[i]))
		{
			bgfx::destroyProgram(program_handles[i]);
		}
	}

	for (auto* binary : binaries)
	{
		if (!binary) continue;

		shader.removeDependency(*binary);
		auto* manager = binary->getResourceManager().get(ResourceManager::SHADER_BINARY);
		manager->unload(*binary);
	}
}


bool Shader::getShaderCombinations(Renderer& renderer,
	const char* shader_content,
	ShaderCombinations* output)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	registerFunctions(nullptr, output, &renderer, L);

	bool errors = luaL_loadbuffer(L, shader_content, stringLength(shader_content), "") != LUA_OK;
	errors = errors || lua_pcall(L, 0, 0, 0) != LUA_OK;
	if (errors)
	{
		g_log_error.log("Renderer") << lua_tostring(L, -1);
		lua_pop(L, 1);
		lua_close(L);
		return false;
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
