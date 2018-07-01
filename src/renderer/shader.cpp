#include "renderer/shader.h"
#include "engine/crc32.h"
#include "engine/fs/file_system.h"
#include "engine/engine.h"
#include "engine/lua_wrapper.h"
#include "engine/log.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "renderer/global_state_uniforms.h"
#include "renderer/renderer.h"
#include "renderer/shader_manager.h"
#include "renderer/texture.h"
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


const ResourceType Shader::TYPE("shader");


Shader::Shader(const Path& path, ResourceManagerBase& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_renderer(renderer)
	, m_texture_slot_count(0)
	, m_uniforms(m_allocator)
	, m_render_states(0)
	, m_all_defines_mask(0)
	, m_sources(m_allocator)
	, m_programs(m_allocator)
	, m_attributes(m_allocator)
	, m_include(m_allocator)
{
}


Shader::~Shader()
{
	ASSERT(isEmpty());
}


const Shader::Program& Shader::getProgram(u32 defines)
{
	auto iter = m_programs.find(defines);
	if (!iter.isValid()) {
		PROFILE_BLOCK("compile_shader");
		static const char* shader_code_prefix = 
			"#version 420\n"
			"layout (std140) uniform GlobalState\n"
			"{\n"
			"	mat4 u_projection;\n"
			"	mat4 u_view;\n"
			"	vec3 u_camera_pos;\n"
			"	vec3 u_light_direction;\n"
			"	vec3 u_light_color;\n"
			"	float u_light_intensity;\n"
			"	float u_light_indirect_intensity;\n"
			"	ivec2 u_framebuffer_size;\n"
			"};\n";

		const char* codes[64];
		ffr::ShaderType types[64];
		ASSERT(lengthOf(types) >= m_sources.size());
		for (int i = 0; i < m_sources.size(); ++i) {
			codes[i] = &m_sources[i].code[0];
			types[i] = m_sources[i].type;
		}
		const char* prefixes[34];
		StaticString<128> defines_code[32];
		int defines_count = 0;
		prefixes[0] = shader_code_prefix;
		prefixes[1] = m_include.empty() ? "" : (const char*)&m_include[0];
		if (defines != 0) {
			for(int i = 0; i < sizeof(defines) * 8; ++i) {
				if((defines & (1 << i)) == 0) continue;
				defines_code[defines_count] << "#define " << m_renderer.getShaderDefine(i) << "\n";
				prefixes[2 + defines_count] = defines_code[defines_count];
				++defines_count;
			}
		}

		Program program;

		for(int& i : program.attribute_by_semantics) i = -1;
		program.handle = ffr::createProgram(codes, types, m_sources.size(), prefixes, 2 + defines_count, getPath().c_str());
		program.use_semantics = false;
		if(program.handle.isValid()) {
			ffr::uniformBlockBinding(program.handle, "GlobalState", 0);
			for(const AttributeInfo& attr : m_attributes) {
				program.use_semantics = true;
				const int loc = ffr::getAttribLocation(program.handle, attr.name);
				if(loc >= 0) {
					program.attribute_by_semantics[(int)attr.semantic] = loc;
				}
			}
		}
		m_programs.insert(defines, program);
		iter = m_programs.find(defines);
	}
	return iter.value();
}


static void registerCFunction(lua_State* L, const char* name, lua_CFunction function)
{
	lua_pushcfunction(L, function);
	lua_setglobal(L, name);
}


static Shader* getShader(lua_State* L)
{
	Shader* ret = nullptr;
	lua_getglobal(L, "shader");
	if (lua_type(L, -1) == LUA_TLIGHTUSERDATA)
	{
		ret = LuaWrapper::toType<Shader*>(L, -1);
	}
	lua_pop(L, 1);
	return ret;
}


static Renderer& getRendererGlobal(lua_State* L)
{
	Renderer* ret = nullptr;
	lua_getglobal(L, "renderer");
	ASSERT(lua_type(L, -1) == LUA_TLIGHTUSERDATA);
	return *LuaWrapper::toType<Renderer*>(L, -1);
}


static void default_texture(lua_State* state, const char* path)
{
	// TODO
	ASSERT(false);
	/*
	Shader* shader = getShader(state);
	if (!shader) return;
	if (shader->m_texture_slot_count == 0) return;
	Shader::TextureSlot& slot = shader->m_texture_slots[shader->m_texture_slot_count - 1];
	ResourceManagerBase* texture_manager = shader->getResourceManager().getOwner().get(Texture::TYPE);
	slot.default_texture = (Texture*)texture_manager->load(Path(path));*/
}


static void texture_slot(lua_State* state, const char* name, const char* uniform)
{
		// TODO
	ASSERT(false);
	/*
Shader* shader = getShader(state);
	if (!shader) return;
	Shader::TextureSlot& slot = shader->m_texture_slots[shader->m_texture_slot_count];
	copyString(slot.name, name);
	slot.uniform_handle = bgfx::createUniform(uniform, bgfx::UniformType::Int1);
	copyString(slot.uniform, uniform);
	++shader->m_texture_slot_count;*/
}


static void texture_define(lua_State* L, const char* define)
{
		// TODO
	ASSERT(false);
	/*
Shader* shader = getShader(L);
	if (!shader) return;
	Renderer& renderer = shader->getRenderer();

	auto& slot = shader->m_texture_slots[shader->m_texture_slot_count - 1];
	slot.define_idx = renderer.getShaderDefineIdx(lua_tostring(L, -1));*/
}


static void uniform(lua_State* L, const char* name, const char* type)
{
	// TODO
	ASSERT(false);
	/*
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
	else if (equalStrings(type, "vec4"))
	{
		u.type = Shader::Uniform::VEC4;
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
	}*/
}


static void alpha_blending(lua_State* L, const char* mode)
{
	// TODO
	ASSERT(false);
	/*
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
	}*/
}


static void depth_test(lua_State* L, bool enabled)
{
		// TODO
	ASSERT(false);
	/*
Shader* shader = nullptr;
	if (lua_getglobal(L, "shader") == LUA_TLIGHTUSERDATA)
	{
		shader = LuaWrapper::toType<Shader*>(L, -1);
	}
	lua_pop(L, 1);
	if (!shader) return;
	if (enabled)
	{
		shader->m_render_states |= BGFX_STATE_DEPTH_TEST_GEQUAL;
	}
	else
	{
		shader->m_render_states &= ~BGFX_STATE_DEPTH_TEST_MASK;
	}*/
}


namespace LuaAPI
{


int attribute(lua_State* L)
{
	LuaWrapper::checkTableArg(L, 1);

	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	Shader::AttributeInfo& info = shader->m_attributes.emplace();
	lua_getfield(L, 1, "name");
	if (lua_isstring(L, -1)) {
		info.name = lua_tostring(L, -1);
	}
	lua_pop(L, 1);
	
	lua_getfield(L, 1, "semantic");
	if (lua_isnumber(L, -1)) {
		info.semantic = (Mesh::AttributeSemantic)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);

	return 0;
}


int texture_slot(lua_State* L)
{
	LuaWrapper::checkTableArg(L, 1);

	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if(shader->m_texture_slot_count >= lengthOf(shader->m_texture_slots)) {
		g_log_error.log("Renderer") << "Too many texture slots in " << shader->getPath();
		return 0;
	}

	Shader::TextureSlot& slot = shader->m_texture_slots[shader->m_texture_slot_count];
	LuaWrapper::getOptionalStringField(L, -1, "uniform", slot.uniform, lengthOf(slot.uniform));

	++shader->m_texture_slot_count;

	return 0;
}


static void source(lua_State* L, ffr::ShaderType shader_type)
{
	auto countLines = [](const char* str) {
		int count = 0;
		const char* c = str;
		while(*c) {
			if(*c == '\n') ++count;
			++c;
		}
		return count;
	};

	const char* src = LuaWrapper::checkArg<const char*>(L, 1);
	
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	Shader::Source& srcobj = shader->m_sources.emplace(shader->m_allocator);
	srcobj.type = shader_type;

	lua_Debug ar;
	lua_getstack(L, 1, &ar);
	lua_getinfo(L, "nSl", &ar);
	const int line = ar.currentline - countLines(src);

	const StaticString<32> line_str("#line ", line, "\n");
	const int line_str_len = stringLength(line_str);
	const int src_len = stringLength(src);

	srcobj.code.resize(line_str_len + src_len + 1);
	copyMemory(&srcobj.code[0], line_str, line_str_len);
	copyMemory(&srcobj.code[line_str_len], src, src_len);
	srcobj.code.back() = '\0';
}


int vertex_shader(lua_State* L)
{
	source(L, ffr::ShaderType::VERTEX);
	return 0;
}


int fragment_shader(lua_State* L)
{
	source(L, ffr::ShaderType::FRAGMENT);
	return 0;
}


int include(lua_State* L)
{
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);

	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (!shader->m_include.empty()) {
		g_log_error.log("Renderer") << "More than 1 include in " << shader->getPath() << ". Max is 1.";
		return 0;
	}

	FS::FileSystem& fs = shader->m_renderer.getEngine().getFileSystem();
	FS::IFile* file = fs.open(fs.getDefaultDevice(), Path(path), FS::Mode::OPEN_AND_READ);
	if (!file) {
		g_log_error.log("Renderer") << "Failed to open include " << path << " included from " << shader->getPath();
		return 0;
	}

	shader->m_include.resize((int)file->size() + 2);
	if (!shader->m_include.empty()) {
		file->read(&shader->m_include[0], shader->m_include.size() - 1);
		shader->m_include[shader->m_include.size() - 2] = '\n';
		shader->m_include.back() = '\0';
	}

	fs.close(*file);
	return 0;
}


} // namespace LuaAPI


bool Shader::load(FS::IFile& file)
{
	lua_State* L = luaL_newstate();

	ASSERT(m_include.empty());
	ASSERT(m_programs.empty());
	ASSERT(m_attributes.empty());
	ASSERT(m_sources.empty());

	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_GLOBALSINDEX, "this");
	lua_pushcclosure(L, LuaAPI::vertex_shader, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "vertex_shader");
	lua_pushcclosure(L, LuaAPI::fragment_shader, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "fragment_shader");
	lua_pushcclosure(L, LuaAPI::include, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "include");
	lua_pushcclosure(L, LuaAPI::texture_slot, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "texture_slot");
	lua_pushcclosure(L, LuaAPI::attribute, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "attribute");

	lua_pushinteger(L, (int)Mesh::AttributeSemantic::POSITION);
	lua_setglobal(L, "SEMANTICS_POSITION");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::COLOR0);
	lua_setglobal(L, "SEMANTICS_COLOR0");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::TEXCOORD0);
	lua_setglobal(L, "SEMANTICS_TEXCOORD0");
	lua_pushinteger(L, (int)Mesh::AttributeSemantic::NORMAL);
	lua_setglobal(L, "SEMANTICS_NORMAL");

	const StringView content((const char*)file.getBuffer(), (int)file.size());
	if (!LuaWrapper::execute(L, content, getPath().c_str(), 0)) {
		lua_close(L);
		return false;
	}

	m_size = file.size();
	lua_close(L);
	return true;

	// TODO
	//m_render_states = BGFX_STATE_DEPTH_TEST_GEQUAL;
}


void Shader::unload()
{
	m_include.clear();
	m_attributes.clear();
	m_sources.clear();
	for(const Program& prg : m_programs) {
		ffr::destroy(prg.handle);
	}
	m_programs.clear();
		// TODO
	/*
	for (auto& uniform : m_uniforms)
	{
		bgfx::destroy(uniform.handle);
	}
	m_uniforms.clear();
	*/
	for (int i = 0; i < m_texture_slot_count; ++i)
	{
/*		if (bgfx::isValid(m_texture_slots[i].uniform_handle))
		{
			bgfx::destroy(m_texture_slots[i].uniform_handle);
		}*/
		if (m_texture_slots[i].default_texture)
		{
			Texture* t = m_texture_slots[i].default_texture;
			t->getResourceManager().unload(*t);
			m_texture_slots[i].default_texture = nullptr;
		}

		//m_texture_slots[i].uniform_handle = BGFX_INVALID_HANDLE;
	}
	m_texture_slot_count = 0;
	m_all_defines_mask = 0;
	/*
	m_instances.clear();

	m_render_states = 0;*/
}


} // namespace Lumix
