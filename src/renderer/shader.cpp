#include "renderer/shader.h"
#include "engine/crc32.h"
#include "engine/file_system.h"
#include "engine/engine.h"
#include "engine/lua_wrapper.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


const ResourceType Shader::TYPE("shader");


u32 Shader::Uniform::size() const {
	switch (type) {
		case INT: return 4;
		case FLOAT: return 4;
		case MATRIX4: return 64;
		case COLOR: return 16;
		case VEC2: return 8;
		case VEC3: return 12;
		case VEC4: return 16;	
	}
	ASSERT(false);
	return 0;
}


Shader::Shader(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, m_renderer(renderer)
	, m_texture_slot_count(0)
	, m_uniforms(m_allocator)
	, m_all_defines_mask(0)
	, m_render_data(nullptr)
	, m_defines(m_allocator)
{
}


Shader::~Shader()
{
	ASSERT(isEmpty());
}

bool Shader::hasDefine(u8 define) const {
	return m_defines.indexOf(define) >= 0;
}

const ffr::ProgramHandle& Shader::getProgram(ShaderRenderData* rd, const ffr::VertexDecl& decl, u32 defines)
{
	ffr::checkThread();
	const u64 key = defines | ((u64)decl.hash << 32);
	auto iter = rd->programs.find(key);
	if (!iter.isValid()) {
		PROFILE_BLOCK("compile_shader");
		static const char* shader_code_prefix = 
			R"#(
			layout (std140, binding = 0) uniform GlobalState {
				mat4 u_shadow_view_projection;
				mat4 u_shadowmap_matrices[4];
				mat4 u_camera_projection;
				mat4 u_camera_inv_projection;
				mat4 u_camera_view;
				mat4 u_camera_inv_view;
				mat4 u_camera_view_projection;
				mat4 u_camera_inv_view_projection;
				vec3 u_light_direction;
				vec3 u_light_color;
				float u_light_intensity;
				float u_light_indirect_intensity;
				float u_time;
				ivec2 u_framebuffer_size;
			};
			layout (std140, binding = 1) uniform PassState {
				mat4 u_pass_projection;
				mat4 u_pass_inv_projection;
				mat4 u_pass_view;
				mat4 u_pass_inv_view;
				mat4 u_pass_view_projection;
				mat4 u_pass_inv_view_projection;
			};
			layout (std140, binding = 2) uniform MaterialState {
				vec4 u_material_color;
				float u_roughness;
				float u_metallic;
				float u_emission;
			};
			layout (binding=14) uniform samplerCube u_irradiancemap;
			layout (binding=15) uniform samplerCube u_radiancemap;
			)#";

		const char* codes[64];
		ffr::ShaderType types[64];
		ASSERT(lengthOf(types) >= rd->sources.size());
		for (int i = 0; i < rd->sources.size(); ++i) {
			codes[i] = &rd->sources[i].code[0];
			types[i] = rd->sources[i].type;
		}
		const char* prefixes[35];
		StaticString<128> defines_code[32];
		int defines_count = 0;
		prefixes[0] = shader_code_prefix;
		if (defines != 0) {
			for(int i = 0; i < sizeof(defines) * 8; ++i) {
				if((defines & (1 << i)) == 0) continue;
				// TODO getShaderDefine is not threadsafe
				defines_code[defines_count] << "#define " << rd->renderer.getShaderDefine(i) << "\n";
				prefixes[1 + defines_count] = defines_code[defines_count];
				++defines_count;
			}
		}
		prefixes[1 + defines_count] = rd->include.empty() ? "" : (const char*)rd->include.begin();
		prefixes[2 + defines_count] = rd->common_source.empty() ? "" : rd->common_source.begin();

		ffr::ProgramHandle program = ffr::allocProgramHandle();
		if(program.isValid() && !ffr::createProgram(program, decl, codes, types, rd->sources.size(), prefixes, 3 + defines_count, rd->path.c_str())) {
			ffr::destroy(program);
			program = ffr::INVALID_PROGRAM;
		}
		rd->programs.insert(key, program);
		iter = rd->programs.find(key);
	}
	return iter.value();
}


static Shader* getShader(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	ASSERT(lua_type(L, -1) == LUA_TLIGHTUSERDATA);
	Shader* shader = (Shader*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return shader;
}


namespace LuaAPI
{
	

int uniform(lua_State* L)
{
	const char* name = LuaWrapper::checkArg<const char*>(L, 1); 
	const char* type = LuaWrapper::checkArg<const char*>(L, 2);
	Shader* shader = getShader(L);
	ASSERT(shader);

	Shader::Uniform& u = shader->m_uniforms.emplace();
	copyString(u.name, name);
	u.name_hash = crc32(name);
	const struct {
		const char* str;
		Shader::Uniform::Type type;
	} types[] = {
		{ "float", Shader::Uniform::FLOAT },
		{ "color", Shader::Uniform::COLOR },
		{ "int", Shader::Uniform::INT },
		{ "mat4", Shader::Uniform::MATRIX4 },
		{ "vec2", Shader::Uniform::VEC2 },
		{ "vec3", Shader::Uniform::VEC3 },
		{ "vec4", Shader::Uniform::VEC4 },
	};

	bool valid = false;
	for (auto& t : types) {
		if (equalStrings(type, t.str)) {
			valid = true;
			u.type = t.type;
			break;
		}
	}

	if (!valid) {
		logError("Renderer") << "Unknown uniform type " << type << " in " << shader->getPath().c_str();
		shader->m_uniforms.pop();
		return 0;
	}

	// TODO std140 layout
	if(shader->m_uniforms.size() == 1) {
		u.offset = 0;
	}
	else {
		const Shader::Uniform& prev = shader->m_uniforms[shader->m_uniforms.size() - 2];
		u.offset = prev.offset + prev.size();
	}
	return 0;
}


int define(lua_State* L)
{
	Shader* shader = getShader(L);
	const char* def = LuaWrapper::checkArg<const char*>(L, 1);

	const u8 def_idx = shader->m_renderer.getShaderDefineIdx(def);
	shader->m_defines.push(def_idx);

	return 0;
}


int texture_slot(lua_State* L)
{
	LuaWrapper::checkTableArg(L, 1);
	Shader* shader = getShader(L);

	if(shader->m_texture_slot_count >= lengthOf(shader->m_texture_slots)) {
		logError("Renderer") << "Too many texture slots in " << shader->getPath();
		return 0;
	}

	Shader::TextureSlot& slot = shader->m_texture_slots[shader->m_texture_slot_count];
	LuaWrapper::getOptionalStringField(L, -1, "name", Span(slot.name));

	char tmp[MAX_PATH_LENGTH];
	if(LuaWrapper::getOptionalStringField(L, -1, "default_texture", Span(tmp))) {
		ResourceManagerHub& manager = shader->getResourceManager().getOwner();
		slot.default_texture = manager.load<Texture>(Path(tmp));
	}

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
	
	Shader* shader = getShader(L);
	Shader::Source& srcobj = shader->m_render_data->sources.emplace(shader->m_allocator);
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


static int common(lua_State* L)
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
	
	Shader* shader = getShader(L);

	lua_Debug ar;
	lua_getstack(L, 1, &ar);
	lua_getinfo(L, "nSl", &ar);
	const int line = ar.currentline - countLines(src);

	const StaticString<32> line_str("#line ", line, "\n");
	const int line_str_len = stringLength(line_str);
	const int src_len = stringLength(src);

	shader->m_render_data->common_source.resize(line_str_len + src_len + 1);
	copyMemory(&shader->m_render_data->common_source[0], line_str, line_str_len);
	copyMemory(&shader->m_render_data->common_source[line_str_len], src, src_len);
	shader->m_render_data->common_source.back() = '\0';
	return 0;
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


int geometry_shader(lua_State* L)
{
	source(L, ffr::ShaderType::GEOMETRY);
	return 0;
}


int include(lua_State* L)
{
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);

	Shader* shader = getShader(L);

	if (!shader->m_render_data->include.empty()) {
		logError("Renderer") << "More than 1 include in " << shader->getPath() << ". Max is 1.";
		return 0;
	}

	FileSystem& fs = shader->m_renderer.getEngine().getFileSystem();

	if (!fs.getContentSync(Path(path), Ref(shader->m_render_data->include))) {
		logError("Renderer") << "Failed to open/read include " << path << " included from " << shader->getPath();
		return 0;
	}
	
	if (!shader->m_render_data->include.empty()) {
		shader->m_render_data->include.resize(shader->m_render_data->include.size() + 2);
		shader->m_render_data->include[shader->m_render_data->include.size() - 2] = '\n';
		shader->m_render_data->include.back() = '\0';
	}

	return 0;
}


} // namespace LuaAPI


bool Shader::load(u64 size, const u8* mem)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	ASSERT(!m_render_data);

	IAllocator& allocator = m_renderer.getAllocator();
	m_render_data = LUMIX_NEW(allocator, ShaderRenderData)(m_renderer, allocator);
	m_render_data->path = getPath();

	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_GLOBALSINDEX, "this");
	lua_pushcfunction(L, LuaAPI::common);
	lua_setfield(L, LUA_GLOBALSINDEX, "common");
	lua_pushcfunction(L, LuaAPI::vertex_shader);
	lua_setfield(L, LUA_GLOBALSINDEX, "vertex_shader");
	lua_pushcfunction(L, LuaAPI::fragment_shader);
	lua_setfield(L, LUA_GLOBALSINDEX, "fragment_shader");
	lua_pushcfunction(L, LuaAPI::geometry_shader);
	lua_setfield(L, LUA_GLOBALSINDEX, "geometry_shader");
	lua_pushcfunction(L, LuaAPI::include);
	lua_setfield(L, LUA_GLOBALSINDEX, "include");
	lua_pushcfunction(L, LuaAPI::texture_slot);
	lua_setfield(L, LUA_GLOBALSINDEX, "texture_slot");
	lua_pushcfunction(L, LuaAPI::define);
	lua_setfield(L, LUA_GLOBALSINDEX, "define");
	lua_pushcfunction(L, LuaAPI::uniform);
	lua_setfield(L, LUA_GLOBALSINDEX, "uniform");

	const Span<const char> content((const char*)mem, (int)size);
	if (!LuaWrapper::execute(L, content, getPath().c_str(), 0)) {
		lua_close(L);
		return false;
	}

	m_size = size;
	lua_close(L);
	return true;
}


void Shader::unload()
{
	if (m_render_data) {
		m_renderer.runInRenderThread(m_render_data, [](Renderer& renderer, void* ptr){
			ShaderRenderData* rd = (ShaderRenderData*)ptr;
			for(const ffr::ProgramHandle prg : rd->programs) {
				if (prg.isValid()) ffr::destroy(prg);
			}
			LUMIX_DELETE(rd->allocator, rd);
		});
		m_render_data = nullptr;
	}
	m_uniforms.clear();
	for (int i = 0; i < m_texture_slot_count; ++i) {
		if (m_texture_slots[i].default_texture) {
			Texture* t = m_texture_slots[i].default_texture;
			t->getResourceManager().unload(*t);
			m_texture_slots[i].default_texture = nullptr;
		}
	}
	m_texture_slot_count = 0;
	m_all_defines_mask = 0;
}


} // namespace Lumix
