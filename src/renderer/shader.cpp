#include "renderer/shader.h"
#include "engine/file_system.h"
#include "engine/engine.h"
#include "engine/hash.h"
#include "engine/lua_wrapper.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "renderer/draw_stream.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"


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
		case VEC3: return 16; // pad to vec4
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
	, m_defines(m_allocator)
	, m_programs(m_allocator)
	, m_sources(m_allocator)
{
	m_sources.path = path;
}


Shader::~Shader()
{
	ASSERT(isEmpty());
}

bool Shader::hasDefine(u8 define) const {
	return m_defines.indexOf(define) >= 0;
}

bool Shader::ShaderKey::operator==(const ShaderKey& rhs) const {
	return memcmp(this, &rhs, sizeof(rhs)) == 0;
}

void Shader::compile(gpu::ProgramHandle program
	, gpu::StateFlags state
	, gpu::VertexDecl decl
	, u32 defines
	, DrawStream& stream
) {
	PROFILE_BLOCK("compile_shader");

	const char* codes[64];
	gpu::ShaderType types[64];
	ASSERT((int)lengthOf(types) >= m_sources.stages.size());
	for (int i = 0; i < m_sources.stages.size(); ++i) {
		codes[i] = &m_sources.stages[i].code[0];
		types[i] = m_sources.stages[i].type;
	}
	const char* prefixes[35];
	StaticString<128> defines_code[32];
	int defines_count = 0;
	if (defines != 0) {
		for(int i = 0; i < sizeof(defines) * 8; ++i) {
			if((defines & (1 << i)) == 0) continue;
			defines_code[defines_count] << "#define " << m_renderer.getShaderDefine(i) << "\n";
			prefixes[defines_count] = defines_code[defines_count];
			++defines_count;
		}
	}
	prefixes[defines_count] = m_sources.common.length() == 0 ? "" : m_sources.common.c_str();

	stream.createProgram(program, state, decl, codes, types, m_sources.stages.size(), prefixes, 1 + defines_count, m_sources.path.c_str());
}

gpu::ProgramHandle Shader::getProgram(gpu::StateFlags state, const gpu::VertexDecl& decl, u32 defines) {
	ShaderKey key;
	key.decl_hash = decl.hash;
	key.defines = defines;
	key.state = state;
	auto iter = m_programs.find(key);
	if (iter.isValid()) return iter.value();
	return m_renderer.queueShaderCompile(*this, state, decl, defines);
}

gpu::ProgramHandle Shader::getProgram(u32 defines) {
	ASSERT(m_sources.stages.empty() || m_sources.stages[0].type == gpu::ShaderType::COMPUTE);
	const gpu::VertexDecl dummy_decl(gpu::PrimitiveType::NONE);
	ShaderKey key;
	key.decl_hash = dummy_decl.hash;
	key.defines = defines;
	key.state = gpu::StateFlags::NONE;
	auto iter = m_programs.find(key);
	if (iter.isValid()) return iter.value();
	return m_renderer.queueShaderCompile(*this, gpu::StateFlags::NONE, dummy_decl, defines);
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
	u.name_hash = RuntimeHash(name);
	memset(&u.default_value, 0, sizeof(u.default_value));
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
		logError("Unknown uniform type ", type, " in ", shader->getPath());
		shader->m_uniforms.pop();
		return 0;
	}

	if (lua_gettop(L) > 2) {
		switch (lua_type(L, 3)) {
			case LUA_TNUMBER: u.default_value.float_value = LuaWrapper::toType<float>(L, 3); break;
			case LUA_TTABLE: {
				const size_t len = lua_objlen(L, 3);
				switch (len) {
					case 2:	*(Vec2*)u.default_value.vec2 = LuaWrapper::toType<Vec2>(L, 3); break;
					case 3: *(Vec3*)u.default_value.vec3 = LuaWrapper::toType<Vec3>(L, 3); break;
					case 4: *(Vec4*)u.default_value.vec4 = LuaWrapper::toType<Vec4>(L, 3); break;
					case 16: *(Matrix*)u.default_value.vec4 = LuaWrapper::toType<Matrix>(L, 3); break;
					default: luaL_error(L, "Uniform %s has unsupported type", name); break;
				}
				break;
			}
			default: luaL_error(L, "Uniform %s has unsupported type", name); break;
		}
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
		logError("Too many texture slots in ", shader->getPath());
		return 0;
	}

	Shader::TextureSlot& slot = shader->m_texture_slots[shader->m_texture_slot_count];
	LuaWrapper::getOptionalStringField(L, -1, "name", Span(slot.name));
	char define[64];
	if (LuaWrapper::getOptionalStringField(L, -1, "define", Span(define))) {
		slot.define_idx = shader->m_renderer.getShaderDefineIdx(define);
	}

	char tmp[LUMIX_MAX_PATH];
	if(LuaWrapper::getOptionalStringField(L, -1, "default_texture", Span(tmp))) {
		ResourceManagerHub& manager = shader->getResourceManager().getOwner();
		slot.default_texture = manager.load<Texture>(Path(tmp));
	}

	++shader->m_texture_slot_count;

	return 0;
}


static void source(lua_State* L, gpu::ShaderType shader_type)
{
	const char* src = LuaWrapper::checkArg<const char*>(L, 1);
	
	Shader* shader = getShader(L);
	Shader::Stage& stage = shader->m_sources.stages.emplace(shader->m_allocator);
	stage.type = shader_type;

	lua_Debug ar;
	lua_getstack(L, 1, &ar);
	lua_getinfo(L, "nSl", &ar);
	const int line = ar.currentline;
	ASSERT(line >= 0);

	const StaticString<32> line_str("#line ", line, "\n");
	const int line_str_len = stringLength(line_str);
	const int src_len = stringLength(src);

	stage.code.resize(line_str_len + src_len + 1);
	memcpy(&stage.code[0], line_str, line_str_len);
	memcpy(&stage.code[line_str_len], src, src_len);
	stage.code.back() = '\0';
}


static int common(lua_State* L)
{
	const char* src = LuaWrapper::checkArg<const char*>(L, 1);
	
	Shader* shader = getShader(L);

	lua_Debug ar;
	lua_getstack(L, 1, &ar);
	lua_getinfo(L, "nSl", &ar);
	const int line = ar.currentline;
	ASSERT(line >= 0);

	const StaticString<32> line_str("#line ", line, "\n");

	shader->m_sources.common
		.cat(line_str.data)
		.cat(src);
	return 0;
}


int vertex_shader(lua_State* L)
{
	source(L, gpu::ShaderType::VERTEX);
	return 0;
}


int fragment_shader(lua_State* L)
{
	source(L, gpu::ShaderType::FRAGMENT);
	return 0;
}


int compute_shader(lua_State* L)
{
	source(L, gpu::ShaderType::COMPUTE);
	return 0;
}


int geometry_shader(lua_State* L)
{
	source(L, gpu::ShaderType::GEOMETRY);
	return 0;
}

int import(lua_State* L)
{
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);
	Shader* shader = getShader(L);
	FileSystem& fs = shader->m_renderer.getEngine().getFileSystem();

	OutputMemoryStream content(shader->m_allocator);
	if (!fs.getContentSync(Path(path), content)) {
		logError("Failed to open/read import ", path, " imported from ", shader->getPath());
		return 0;
	}
	
	if (!content.empty()) {
		LuaWrapper::execute(L, Span((const char*)content.data(), (u32)content.size()), path, 0);
	}

	return 0;
}

int include(lua_State* L)
{
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);

	Shader* shader = getShader(L);

	FileSystem& fs = shader->m_renderer.getEngine().getFileSystem();

	OutputMemoryStream content(shader->m_allocator);
	if (!fs.getContentSync(Path(path), content)) {
		logError("Failed to open/read include ", path, " included from ", shader->getPath());
		return 0;
	}
	
	if (!content.empty()) {
		content << "\n";
		shader->m_sources.common.cat("#line 0\n");
		shader->m_sources.common.cat(Span((const char*)content.data(), (u32)content.size()));
	}

	return 0;
}


} // namespace LuaAPI


bool Shader::load(u64 size, const u8* mem)
{
	lua_State* root_state = m_renderer.getEngine().getState();
	lua_State* L = lua_newthread(root_state);
	const int state_ref = luaL_ref(root_state, LUA_REGISTRYINDEX);

	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_GLOBALSINDEX, "this");
	lua_pushcfunction(L, LuaAPI::common);
	lua_setfield(L, LUA_GLOBALSINDEX, "common");
	lua_pushcfunction(L, LuaAPI::vertex_shader);
	lua_setfield(L, LUA_GLOBALSINDEX, "vertex_shader");
	lua_pushcfunction(L, LuaAPI::fragment_shader);
	lua_setfield(L, LUA_GLOBALSINDEX, "fragment_shader");
	lua_pushcfunction(L, LuaAPI::compute_shader);
	lua_setfield(L, LUA_GLOBALSINDEX, "compute_shader");
	lua_pushcfunction(L, LuaAPI::geometry_shader);
	lua_setfield(L, LUA_GLOBALSINDEX, "geometry_shader");
	lua_pushcfunction(L, LuaAPI::include);
	lua_setfield(L, LUA_GLOBALSINDEX, "include");
	lua_pushcfunction(L, LuaAPI::import);
	lua_setfield(L, LUA_GLOBALSINDEX, "import");
	lua_pushcfunction(L, LuaAPI::texture_slot);
	lua_setfield(L, LUA_GLOBALSINDEX, "texture_slot");
	lua_pushcfunction(L, LuaAPI::define);
	lua_setfield(L, LUA_GLOBALSINDEX, "define");
	lua_pushcfunction(L, LuaAPI::uniform);
	lua_setfield(L, LUA_GLOBALSINDEX, "uniform");

	const Span<const char> content((const char*)mem, (int)size);
	if (!LuaWrapper::execute(L, content, getPath().c_str(), 0)) {
		luaL_unref(root_state, LUA_REGISTRYINDEX, state_ref);
		return false;
	}

	luaL_unref(root_state, LUA_REGISTRYINDEX, state_ref);
	return true;
}


void Shader::unload()
{
	for (gpu::ProgramHandle prg : m_programs) {
		m_renderer.getDrawStream().destroy(prg);
	}
	m_sources.common = "";
	m_sources.stages.clear();
	m_programs.clear();
	m_uniforms.clear();
	for (u32 i = 0; i < m_texture_slot_count; ++i) {
		if (m_texture_slots[i].default_texture) {
			Texture* t = m_texture_slots[i].default_texture;
			t->decRefCount();
			m_texture_slots[i].default_texture = nullptr;
		}
	}
	m_texture_slot_count = 0;
	m_all_defines_mask = 0;
}

static const char* toString(Shader::Uniform::Type type) {
	switch(type) {
		case Shader::Uniform::COLOR: return "vec4";
		case Shader::Uniform::FLOAT: return "float";
		case Shader::Uniform::INT: return "int";
		case Shader::Uniform::MATRIX4: return "mat4";
		case Shader::Uniform::VEC2: return "vec2";
		case Shader::Uniform::VEC3: return "vec4"; // vec4 because of padding padding
		case Shader::Uniform::VEC4: return "vec4";
		default: ASSERT(false); return "unknown_type";
	}
}

static void toName(char prefix, Span<char> out, const char* in) {
	ASSERT(out.length() > 3);
	const char* c = in;
	char* o = out.begin();
	*o = prefix;
	++o;
	*o = '_';
	++o;
	while (o != out.end() - 1 && *c) {
		if (*c >= 'a' && *c <= 'z' || *c >= '0' && *c <= '9') {
			*o = *c;
		}
		else if (*c >= 'A' && *c <= 'Z') {
			*o = *c + ('a' - 'A');
		}
		else {
			*o = '_';
		}
		++o;
		++c;
	}
	*o = 0;
}

void Shader::toUniformVarName(Span<char> out, const char* in) {
	toName('u', out, in);
}

void Shader::toTextureVarName(Span<char> out, const char* in) {
	toName('t', out, in);
}

void Shader::onBeforeReady() {
	if (m_uniforms.empty()) return;

	m_sources.common.cat("layout (std140, binding = 2) uniform MaterialState {");

	for (const Uniform& u : m_uniforms) {
		m_sources.common.cat(toString(u.type));
		m_sources.common.cat(" ");
		char var_name[64];
		toUniformVarName(Span(var_name), u.name);
		m_sources.common.cat(var_name);
		m_sources.common.cat(";\n");
	}

	m_sources.common.cat("};\n");
}


} // namespace Lumix
