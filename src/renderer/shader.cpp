#include "renderer/shader.h"
#include "engine/file_system.h"
#include "engine/engine.h"
#include "core/hash.h"
#include "engine/lua_wrapper.h"
#include "core/log.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/tokenizer.h"
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
		case NORMALIZED_FLOAT: return 4;
		case FLOAT: return 4;
		case COLOR: return 16;
		case FLOAT2: return 8;
		case FLOAT3: return 16; // pad to vec4
		case FLOAT4: return 16;	
	}
	ASSERT(false);
	return 0;
}


Shader::Shader(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator, m_path.c_str())
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


bool Shader::hasDefine(u8 define) const {
	return m_defines.indexOf(define) >= 0;
}

bool ShaderKey::operator==(const ShaderKey& rhs) const {
	return memcmp(this, &rhs, sizeof(rhs)) == 0;
}

void Shader::compile(gpu::ProgramHandle program, const ShaderKey& key, gpu::VertexDecl decl, DrawStream& stream) {
	PROFILE_BLOCK("compile_shader");

	const char* codes[64];
	gpu::ShaderType types[64];
	ASSERT((int)lengthOf(types) >= m_sources.stages.size());
	for (int i = 0; i < m_sources.stages.size(); ++i) {
		codes[i] = &m_sources.stages[i].code[0];
		types[i] = m_sources.stages[i].type;
	}
	const char* prefixes[36];
	StaticString<128> defines_code[32];
	int defines_count = 0;
	prefixes[0] = key.semantic_defines ? key.semantic_defines : "";
	if (key.defines != 0) {
		for(int i = 0; i < sizeof(key.defines) * 8; ++i) {
			if((key.defines & (1 << i)) == 0) continue;
			defines_code[defines_count].append("#define ", m_renderer.getShaderDefine(i), "\n");
			prefixes[defines_count + 1] = defines_code[defines_count];
			++defines_count;
		}
	}
	prefixes[defines_count + 1] = m_sources.common.length() == 0 ? "" : m_sources.common.c_str();

	stream.createProgram(program, key.state, decl, codes, types, m_sources.stages.size(), prefixes, 2 + defines_count, m_sources.path.c_str());
}

gpu::ProgramHandle Shader::getProgram(gpu::StateFlags state, const gpu::VertexDecl& decl, u32 defines, const char* semantic_defines) {
	ShaderKey key;
	key.decl_hash = decl.hash;
	key.defines = defines;
	key.state = state;
	key.semantic_defines = semantic_defines;
	for (const ProgramPair& p : m_programs) {
		if (p.key == key) return p.program;
	}
	return m_renderer.queueShaderCompile(*this, key, decl);
}

static gpu::VertexDecl merge(const gpu::VertexDecl& a, const gpu::VertexDecl& b) {
	gpu::VertexDecl res = a;
	for (u32 i = 0; i < b.attributes_count; ++i) {
		res.attributes[res.attributes_count] = b.attributes[i];
		++res.attributes_count;
	}
	res.computeHash();
	return res;
}

gpu::ProgramHandle Shader::getProgram(gpu::StateFlags state, const gpu::VertexDecl& decl, const gpu::VertexDecl& decl2, u32 defines, const char* semantic_defines) {
	ShaderKey key;
	key.decl_hash = decl.hash ^ decl2.hash;
	key.defines = defines;
	key.state = state;
	key.semantic_defines = semantic_defines;

	for (const ProgramPair& p : m_programs) {
		if (p.key == key) return p.program;
	}
	
	gpu::VertexDecl merged_decl = merge(decl, decl2);

	return m_renderer.queueShaderCompile(*this, key, merged_decl);
}

gpu::ProgramHandle Shader::getProgram(u32 defines) {
	ASSERT(m_sources.stages.empty() || m_sources.stages[0].type == gpu::ShaderType::COMPUTE);
	const gpu::VertexDecl dummy_decl(gpu::PrimitiveType::NONE);
	ShaderKey key;
	static const char* no_def = "";
	key.decl_hash = dummy_decl.hash;
	key.defines = defines;
	key.state = gpu::StateFlags::NONE;
	key.semantic_defines = no_def;
	for (const ProgramPair& p : m_programs) {
		if (p.key == key) return p.program;
	}
	return m_renderer.queueShaderCompile(*this, key, dummy_decl);
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
		{ "normalized_float", Shader::Uniform::NORMALIZED_FLOAT },
		{ "float", Shader::Uniform::FLOAT },
		{ "color", Shader::Uniform::COLOR },
		{ "int", Shader::Uniform::INT },
		{ "vec2", Shader::Uniform::FLOAT2 },
		{ "vec3", Shader::Uniform::FLOAT3 },
		{ "vec4", Shader::Uniform::FLOAT4 },
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

	if(shader->m_uniforms.size() == 1) {
		u.offset = 0;
	}
	else {
		const Shader::Uniform& prev = shader->m_uniforms[shader->m_uniforms.size() - 2];
		u.offset = prev.offset + prev.size();
		const u32 align = u.size();
		u.offset += (align - u.offset % align) % align;
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

	Path tmp;
	if(LuaWrapper::getOptionalStringField(L, -1, "default_texture", Span(tmp.beginUpdate(), tmp.capacity()))) {
		tmp.endUpdate();
		ResourceManagerHub& manager = shader->getResourceManager().getOwner();
		slot.default_texture = manager.load<Texture>(tmp);
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
	lua_getinfo(L, 1, "nsl", &ar);
	const int line = ar.currentline;
	ASSERT(line >= 0);

	const StaticString<32 + MAX_PATH> line_str("#line ", line, "\"", shader->getPath(), "\"", "\n");
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
	lua_getinfo(L, 1, "nsl", &ar);
	const int line = ar.currentline;
	ASSERT(line >= 0);

	const StaticString<32> line_str("#line ", line, "\n");

	shader->m_sources.common.append(line_str, src);
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


int geometry_shader(lua_State* L)
{
	source(L, gpu::ShaderType::GEOMETRY);
	return 0;
}

int import(lua_State* L)
{
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);
	Shader* shader = getShader(L);

	OutputMemoryStream content(shader->m_allocator);
	ResourceManagerHub& rm = shader->getResourceManager().getOwner();
	
	if (!rm.loadRaw(shader->getPath(), Path(path), content)) {
		logError("Failed to open/read import ", path, " imported from ", shader->getPath());
		return 0;
	}
	
	if (!content.empty()) {
		LuaWrapper::execute(L, StringView((const char*)content.data(), (u32)content.size()), path, 0);
	}

	return 0;
}

int include(lua_State* L)
{
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);

	Shader* shader = getShader(L);

	ResourceManagerHub& rm = shader->getResourceManager().getOwner();

	OutputMemoryStream content(shader->m_allocator);
	if (!rm.loadRaw(shader->getPath(), Path(path), content)) {
		logError("Failed to open/read include ", path, " included from ", shader->getPath());
		return 0;
	}
	
	if (!content.empty()) {
		content << "\n";
		shader->m_sources.common.append(StringView((const char*)content.data(), (u32)content.size()));
	}

	return 0;
}


} // namespace LuaAPI

static StringView getLine(StringView& src) {
	const char* b = src.begin;
	while (b < src.end && *b != '\n') ++b;
	StringView ret(src.begin, b);
	src.begin = b < src.end ? b + 1 : b;
	return ret;
}

static bool assign(Shader::Uniform& u, Tokenizer::Variant v) {
	switch (u.type) {
		case Shader::Uniform::NORMALIZED_FLOAT:
		case Shader::Uniform::FLOAT:
		case Shader::Uniform::INT:
			if (v.type != Tokenizer::Variant::NUMBER) return false;
			u.default_value.float_value = (float)v.number;
			return true;
		case Shader::Uniform::FLOAT2:
			if (v.type != Tokenizer::Variant::VEC2) return false;
			memcpy(u.default_value.vec2, v.vector, sizeof(u.default_value.vec2));
			return true;
		case Shader::Uniform::FLOAT3:
			if (v.type != Tokenizer::Variant::VEC3) return false;
			memcpy(u.default_value.vec3, v.vector, sizeof(u.default_value.vec3));
			return true;
		case Shader::Uniform::COLOR:
		case Shader::Uniform::FLOAT4:
			if (v.type != Tokenizer::Variant::VEC4) return false;
			memcpy(u.default_value.vec4, v.vector, sizeof(u.default_value.vec4));
			return true;
	}
	ASSERT(false);
	return false;
}

bool Shader::load(Span<const u8> mem) {
	StringView content((const char*)mem.begin(), (u32)mem.length());
	if (Path::hasExtension(getPath(), "hlsl")) {
		StringView preprocess = content;
		bool is_surface = false;
		for (;;) {
			StringView line = getLine(preprocess);
			if (line.begin == preprocess.end) break;
			if (startsWith(line, "//@")) {
				line.removePrefix(3);
				if (startsWith(line, "surface")) {
					is_surface = true;
				}
				else if (startsWith(line, "define \"")) {
					line.removePrefix(8);
					line.end = line.begin + 1;
					while (line.end < preprocess.end && *line.end != '"') ++line.end;
					
					char tmp[64];
					copyString(tmp, line);
					const u8 def_idx = m_renderer.getShaderDefineIdx(tmp);
					m_defines.push(def_idx);
				}
				else if (startsWith(line, "uniform")) {
					line.removePrefix(7);
					Tokenizer t(preprocess, getPath().c_str());
					t.cursor = line.begin;
					StringView name;
					StringView type;
					if (!t.consume(name, ",", type, ",")) return false;

					Shader::Uniform& u = m_uniforms.emplace();
					copyString(Span(u.name), name);
					u.name_hash = RuntimeHash(name.begin, name.size());
									
					if (equalStrings(type, "normalized_float")) u.type = Shader::Uniform::FLOAT;
					else if (equalStrings(type, "float")) u.type = Shader::Uniform::FLOAT;
					else if (equalStrings(type, "int")) u.type = Shader::Uniform::INT;
					else if (equalStrings(type, "color")) u.type = Shader::Uniform::COLOR;
					else if (equalStrings(type, "float2")) u.type = Shader::Uniform::FLOAT2;
					else if (equalStrings(type, "float3")) u.type = Shader::Uniform::FLOAT3;
					else if (equalStrings(type, "float4")) u.type = Shader::Uniform::FLOAT4;
					else {
						logError(getPath(), "(", getLine(type), "): Unknown uniform type ", type, " in ", getPath());
						t.logErrorPosition(type.begin);
						return false;
					}

					Tokenizer::Variant v = t.consumeVariant();
					if (v.type == Tokenizer::Variant::NONE) return false;
					if (!assign(u, v)) {
						logError(getPath(), "(", getLine(type), "): Uniform ", name, " has incompatible type ", type);
						t.logErrorPosition(type.begin);
						return false;
					}

					if (m_uniforms.size() == 1) {
						u.offset = 0;
					}
					else {
						const Shader::Uniform& prev = m_uniforms[m_uniforms.size() - 2];
						u.offset = prev.offset + prev.size();
						const u32 align = u.size();
						u.offset += (align - u.offset % align) % align;
					}

				}
				else if (startsWith(line, "texture_slot")) {
					line.removePrefix(12);
					Tokenizer t(preprocess, getPath().c_str());
					t.content.end = line.end;
					t.cursor = line.begin;
					StringView name;
					StringView default_texture;
					if (!t.consume(name, ",", default_texture)) return false;
					
					Shader::TextureSlot& slot = m_texture_slots[m_texture_slot_count];
					++m_texture_slot_count;
					copyString(slot.name, name);
					
					ResourceManagerHub& manager = getResourceManager().getOwner();
					slot.default_texture = default_texture.empty() ? nullptr : manager.load<Texture>(Path(default_texture));
					
					Tokenizer::Token n = t.tryNextToken();
					if (n && n.value[0] == ',') {
						StringView def;
						if (!t.consume(def)) return false;
						StaticString<64> tmp(def);
						slot.define_idx = m_renderer.getShaderDefineIdx(tmp);
					}
				}
				else if (startsWith(line, "include \"")) {
					StringView path;
					path.begin = line.begin + 9;
					path.end = path.begin + 1;
					while (path.end < preprocess.end && *path.end != '"') ++path.end;

					ResourceManagerHub& rm = getResourceManager().getOwner();
					OutputMemoryStream include_content(m_allocator);
					if (!rm.loadRaw(getPath(), Path(path), include_content)) {
						logError("Failed to open/read include ", path, " included from ", getPath());
						return false;
					}
					
					if (!include_content.empty()) {
						include_content << "\n";
						m_sources.common.append(StringView((const char*)include_content.data(), (u32)include_content.size()));
					}				
				}
			}
		}
		
		Shader::Stage& stage = m_sources.stages.emplace(m_allocator);
		stage.type = is_surface ? gpu::ShaderType::SURFACE : gpu::ShaderType::COMPUTE;
		stage.code.resize(mem.length() + 1);
		memcpy(&stage.code[0], mem.begin(), mem.length());
		stage.code.back() = '\0';
	}
	else {
		lua_State* root_state = m_renderer.getEngine().getState();
		lua_State* L = lua_newthread(root_state);
		const int state_ref = LuaWrapper::createRef(root_state);
		lua_pop(root_state, 1);
		
		lua_pushlightuserdata(L, this);
		lua_setfield(L, LUA_GLOBALSINDEX, "this");
		lua_pushcfunction(L, LuaAPI::common, "common");
		lua_setfield(L, LUA_GLOBALSINDEX, "common");
		lua_pushcfunction(L, LuaAPI::vertex_shader, "vertex_shader");
		lua_setfield(L, LUA_GLOBALSINDEX, "vertex_shader");
		lua_pushcfunction(L, LuaAPI::fragment_shader, "fragment_shader");
		lua_setfield(L, LUA_GLOBALSINDEX, "fragment_shader");
		lua_pushcfunction(L, LuaAPI::geometry_shader, "geometry_shader");
		lua_setfield(L, LUA_GLOBALSINDEX, "geometry_shader");
		lua_pushcfunction(L, LuaAPI::include, "include");
		lua_setfield(L, LUA_GLOBALSINDEX, "include");
		lua_pushcfunction(L, LuaAPI::import, "import");
		lua_setfield(L, LUA_GLOBALSINDEX, "import");
		lua_pushcfunction(L, LuaAPI::texture_slot, "texture_slot");
		lua_setfield(L, LUA_GLOBALSINDEX, "texture_slot");
		lua_pushcfunction(L, LuaAPI::define, "define");
		lua_setfield(L, LUA_GLOBALSINDEX, "define");
		lua_pushcfunction(L, LuaAPI::uniform, "uniform");
		lua_setfield(L, LUA_GLOBALSINDEX, "uniform");

		if (!LuaWrapper::execute(L, content, getPath().c_str(), 0)) {
			LuaWrapper::releaseRef(root_state, state_ref);
			return false;
		}
		LuaWrapper::releaseRef(root_state, state_ref);
	}

	RollingHasher hasher;
	hasher.begin();
	for (auto& stage : m_sources.stages) {
		hasher.update(stage.code.data(), stage.code.size());
	}
	hasher.update(m_sources.common.c_str(), m_sources.common.length());
	m_content_hash = hasher.end();

	return true;
}


void Shader::unload()
{
	for (const ProgramPair& p : m_programs) {
		m_renderer.getEndFrameDrawStream().destroy(p.program);
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
		case Shader::Uniform::COLOR: return "float4";
		case Shader::Uniform::FLOAT: return "float";
		case Shader::Uniform::NORMALIZED_FLOAT: return "float";
		case Shader::Uniform::INT: return "int";
		case Shader::Uniform::FLOAT2: return "float2";
		case Shader::Uniform::FLOAT3: return "float4"; // float4 because of padding
		case Shader::Uniform::FLOAT4: return "float4";
	}
	ASSERT(false);
	return "unknown_type";
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
		if ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')) {
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
	if (m_uniforms.empty() && m_texture_slot_count == 0) return;

	m_sources.common.append("cbuffer MaterialState : register(b2) {");

	for (const Uniform& u : m_uniforms) {
		char var_name[64];
		toUniformVarName(Span(var_name), u.name);
		m_sources.common.append(toString(u.type), " ", var_name, ";\n");
	}

	for (u32 i = 0; i < m_texture_slot_count; ++i) {
		char var_name[64];
		toTextureVarName(Span(var_name), m_texture_slots[i].name);
		m_sources.common.append("uint ", var_name, ";\n");
	}

	m_sources.common.append("};\n");
}


} // namespace Lumix
