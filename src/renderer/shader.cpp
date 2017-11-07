#include "renderer/shader.h"
#include "engine/crc32.h"
#include "engine/fs/file_system.h"
#include "engine/lua_wrapper.h"
#include "engine/log.h"
#include "engine/path_utils.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "renderer/renderer.h"
#include "renderer/shader_manager.h"
#include <bgfx/bgfx.h>
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


static const ResourceType SHADER_TYPE("shader");
static const ResourceType SHADER_BINARY_TYPE("shader_binary");


Shader::Shader(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
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


bool Shader::hasDefine(u8 define_idx) const
{
	return (m_combintions.all_defines_mask & (1 << define_idx)) != 0;
}


ShaderInstance& Shader::getInstance(u32 mask)
{
	mask = mask & m_all_defines_mask;
	for (int i = 0; i < m_instances.size(); ++i)
	{
		if (m_instances[i].define_mask == mask)
		{
			return m_instances[i];
		}
	}

	g_log_error.log("Renderer") << "Unknown shader combination requested: " << mask;
	return m_instances[0];
}


ShaderCombinations::ShaderCombinations()
{
	setMemory(this, 0, sizeof(*this));
}


Renderer& Shader::getRenderer()
{
	return static_cast<ShaderManager&>(m_resource_manager).getRenderer();
}


static u32 getDefineMaskFromDense(const Shader& shader, u32 dense)
{
	u32 mask = 0;
	int defines_count = Math::minimum(lengthOf(shader.m_combintions.defines), int(sizeof(dense) * 8));

	for (int i = 0; i < defines_count; ++i)
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
	bool is_opengl = bgfx::getRendererType() == bgfx::RendererType::OpenGL ||
		bgfx::getRendererType() == bgfx::RendererType::OpenGLES;
	m_instances.clear();

	u32 count = 1 << m_combintions.define_count;

	auto* binary_manager = m_resource_manager.getOwner().get(SHADER_BINARY_TYPE);
	char basename[MAX_PATH_LENGTH];
	PathUtils::getBasename(basename, sizeof(basename), getPath().c_str());

	m_instances.reserve(count);
	for (u32 mask = 0; mask < count; ++mask)
	{
		ShaderInstance& instance = m_instances.emplace(*this);

		instance.define_mask = getDefineMaskFromDense(*this, mask);
		m_all_defines_mask |= instance.define_mask;

		for (int pass_idx = 0; pass_idx < m_combintions.pass_count; ++pass_idx)
		{
			const char* pass = m_combintions.passes[pass_idx];
			StaticString<MAX_PATH_LENGTH> path("pipelines/compiled", is_opengl ? "_gl/" : "/");
			int actual_mask = mask & m_combintions.vs_local_mask[pass_idx];
			path << basename << "_" << pass << actual_mask << "_vs.shb";

			Path vs_path(path);
			auto* vs_binary = static_cast<ShaderBinary*>(binary_manager->load(vs_path));
			addDependency(*vs_binary);
			instance.binaries[pass_idx * 2] = vs_binary;

			path.data[0] = '\0';
			actual_mask = mask & m_combintions.fs_local_mask[pass_idx];
			path << "pipelines/compiled" << (is_opengl ? "_gl/" : "/") << basename;
			path << "_" << pass << actual_mask << "_fs.shb";

			Path fs_path(path);
			auto* fs_binary = static_cast<ShaderBinary*>(binary_manager->load(fs_path));
			addDependency(*fs_binary);
			instance.binaries[pass_idx * 2 + 1] = fs_binary;
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


static Renderer* getRendererGlobal(lua_State* L)
{
	Renderer* renderer = nullptr;
	if (lua_getglobal(L, "renderer") == LUA_TLIGHTUSERDATA)
	{
		renderer = LuaWrapper::toType<Renderer*>(L, -1);
	}
	lua_pop(L, 1);

	if (!renderer)
	{
		g_log_error.log("Renderer") << "Error executing function texture_define, missing renderer global variable";
	}
	return renderer;
}


static void texture_define(lua_State* L, const char* define)
{
	auto* shader = getShader(L);
	if (!shader) return;
	Renderer* renderer = getRendererGlobal(L);
	if (!renderer) return;

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
	if (equalStrings(type, "float"))
	{
		u.type = Shader::Uniform::FLOAT;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (equalStrings(type, "color"))
	{
		u.type = Shader::Uniform::COLOR;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (equalStrings(type, "int"))
	{
		u.type = Shader::Uniform::INT;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Int1);
	}
	else if (equalStrings(type, "matrix4"))
	{
		u.type = Shader::Uniform::MATRIX4;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Mat4);
	}
	else if (equalStrings(type, "time"))
	{
		u.type = Shader::Uniform::TIME;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (equalStrings(type, "vec3"))
	{
		u.type = Shader::Uniform::VEC3;
		u.handle = bgfx::createUniform(name, bgfx::UniformType::Vec4);
	}
	else if (equalStrings(type, "vec2"))
	{
		u.type = Shader::Uniform::VEC2;
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


static int indexOf(ShaderCombinations& combination, u8 define_idx)
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
	if (equalStrings(mode, "add"))
	{
		shader->m_render_states |= BGFX_STATE_BLEND_ADD;
	}
	else if (equalStrings(mode, "alpha"))
	{
		shader->m_render_states |= BGFX_STATE_BLEND_ALPHA;
	}
	else
	{
		g_log_error.log("Renderer") << "Uknown blend mode " << mode << " in " << shader->getPath().c_str();
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
	Renderer* renderer = getRendererGlobal(L);
	if (!renderer) return;

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
		lua_pop(L, 1);
	}
}


static void vs(lua_State* L)
{
	auto* cmb = getCombinations(L);
	Renderer* renderer = getRendererGlobal(L);
	if (!renderer) return;

	LuaWrapper::checkTableArg(L, 1);
	int len = (int)lua_rawlen(L, 1);
	for (int i = 0; i < len; ++i)
	{
		if (lua_rawgeti(L, 1, i + 1) == LUA_TSTRING)
		{
			const char* tmp = lua_tostring(L, -1);
			int define_idx = renderer->getShaderDefineIdx(tmp);
			cmb->all_defines_mask |= 1 << define_idx;
			cmb->vs_local_mask[cmb->pass_count - 1] |= 1 << indexOf(*cmb, define_idx);
		}
		lua_pop(L, 1);
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
	registerCFunction(L, "depth_test", &LuaWrapper::wrap<decltype(&depth_test), depth_test>);
	registerCFunction(L, "alpha_blending", &LuaWrapper::wrap<decltype(&alpha_blending), alpha_blending>);
	registerCFunction(L, "texture_slot", &LuaWrapper::wrap<decltype(&texture_slot), texture_slot>);
	registerCFunction(L, "texture_define", &LuaWrapper::wrap<decltype(&texture_define), texture_define>);
	registerCFunction(L, "uniform", &LuaWrapper::wrap<decltype(&uniform), uniform>);
}


bool Shader::load(FS::IFile& file)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	registerFunctions(this, &m_combintions, &getRenderer(), L);
	m_render_states = BGFX_STATE_DEPTH_TEST_LEQUAL;

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


void Shader::unload(void)
{
	m_combintions = {};

	for (auto& uniform : m_uniforms)
	{
		bgfx::destroy(uniform.handle);
	}
	m_uniforms.clear();

	for (int i = 0; i < m_texture_slot_count; ++i)
	{
		if (bgfx::isValid(m_texture_slots[i].uniform_handle))
		{
			bgfx::destroy(m_texture_slots[i].uniform_handle);
		}
		m_texture_slots[i].uniform_handle = BGFX_INVALID_HANDLE;
	}
	m_texture_slot_count = 0;

	m_instances.clear();

	m_all_defines_mask = 0;
	m_render_states = 0;
}


bgfx::ProgramHandle ShaderInstance::getProgramHandle(int pass_idx)
{
	if (!bgfx::isValid(program_handles[pass_idx]))
	{
		for (int i = 0; i < lengthOf(shader.m_combintions.passes); ++i)
		{
			auto& pass = shader.m_combintions.passes[i];
			int global_idx = shader.getRenderer().getPassIdx(pass);
			if (global_idx == pass_idx)
			{
				int binary_index = i * 2;
				if (!binaries[binary_index] || !binaries[binary_index + 1]) break;
				auto vs_handle = binaries[binary_index]->getHandle();
				auto fs_handle = binaries[binary_index + 1]->getHandle();
				auto program = bgfx::createProgram(vs_handle, fs_handle);
				program_handles[global_idx] = program;
				break;
			}
		}
	}

	return program_handles[pass_idx];
}


ShaderInstance::~ShaderInstance()
{
	for (int i = 0; i < lengthOf(program_handles); ++i)
	{
		if (bgfx::isValid(program_handles[i]))
		{
			bgfx::destroy(program_handles[i]);
		}
	}

	for (auto* binary : binaries)
	{
		if (!binary) continue;

		shader.removeDependency(*binary);
		binary->getResourceManager().unload(*binary);
	}
}


void Shader::onBeforeEmpty()
{
	for (ShaderInstance& inst : m_instances)
	{
		for (int i = 0; i < lengthOf(inst.program_handles); ++i)
		{
			if (bgfx::isValid(inst.program_handles[i]))
			{
				bgfx::destroy(inst.program_handles[i]);
				inst.program_handles[i] = BGFX_INVALID_HANDLE;
			}
		}
	}
}


bool Shader::getShaderCombinations(const char* shd_path,
	Renderer& renderer,
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
		g_log_error.log("Renderer") << shd_path << " - " << lua_tostring(L, -1);
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}
	lua_close(L);
	return true;
}


ShaderBinary::ShaderBinary(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_handle(BGFX_INVALID_HANDLE)
{
}


void ShaderBinary::unload()
{
	if (bgfx::isValid(m_handle)) bgfx::destroy(m_handle);
	m_handle = BGFX_INVALID_HANDLE;
}


bool ShaderBinary::load(FS::IFile& file)
{
	auto* mem = bgfx::alloc((u32)file.size() + 1);
	file.read(mem->data, file.size());
	mem->data[file.size()] = '\0';
	m_handle = bgfx::createShader(mem);
	m_size = file.size();
	if (!bgfx::isValid(m_handle))
	{
		g_log_error.log("Renderer") << getPath().c_str() << ": Failed to create bgfx shader";
		return false;
	}
	bgfx::setName(m_handle, getPath().c_str());
	return true;
}


} // namespace Lumix
