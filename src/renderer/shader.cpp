#include "renderer/shader.h"
#include "engine/file_system.h"
#include "engine/engine.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/tokenizer.h"
#include "engine/resource_manager.h"
#include "renderer/draw_stream.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"

namespace Lumix {

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
	, m_code(m_allocator)
{}

bool Shader::hasDefine(u8 define) const {
	return m_defines.indexOf(define) >= 0;
}

bool ShaderKey::operator==(const ShaderKey& rhs) const {
	return memcmp(this, &rhs, sizeof(rhs)) == 0;
}

void Shader::compile(gpu::ProgramHandle program, const ShaderKey& key, gpu::VertexDecl decl, DrawStream& stream) {
	PROFILE_BLOCK("compile_shader");
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

	stream.createProgram(program, key.state, decl, m_code.c_str(), m_type, prefixes, 1 + defines_count, getPath().c_str());
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
	ASSERT(m_type == gpu::ShaderType::COMPUTE);
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
	StringView preprocess((const char*)mem.begin(), (u32)mem.length());
	bool is_surface = false;
	// TODO move this to asset compiler
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
					m_code.append("#line 1 \"", path, "\"\n");
					m_code.append(StringView((const char*)include_content.data(), (u32)include_content.size()));
				}
			}
		}
	}
	
	m_type = is_surface ? gpu::ShaderType::SURFACE : gpu::ShaderType::COMPUTE;
	m_code.append("#line 1 \"", getPath(), "\"\n");
	m_code.append(mem);

	RollingHasher hasher;
	hasher.begin();
	hasher.update(m_code.c_str(), m_code.length());
	m_content_hash = hasher.end();

	return true;
}


void Shader::unload()
{
	for (const ProgramPair& p : m_programs) {
		m_renderer.getEndFrameDrawStream().destroy(p.program);
	}
	m_code = "";
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

	String tmp(m_allocator);
	tmp.append("cbuffer MaterialState : register(b2) {");

	for (const Uniform& u : m_uniforms) {
		char var_name[64];
		toUniformVarName(Span(var_name), u.name);
		tmp.append(toString(u.type), " ", var_name, ";\n");
	}

	for (u32 i = 0; i < m_texture_slot_count; ++i) {
		char var_name[64];
		toTextureVarName(Span(var_name), m_texture_slots[i].name);
		tmp.append("uint ", var_name, ";\n");
	}

	tmp.append("};\n");
	m_code.insert(0, tmp);
}


} // namespace Lumix
