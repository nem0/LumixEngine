#include "renderer/shader.h"
#include "engine/file_system.h"
#include "engine/engine.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/path.h"
#include "core/profiler.h"
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

bool Shader::load(Span<const u8> mem) {
	InputMemoryStream stream(mem);
	Header header;
	stream.read(header);
	if (header.magic != Header::MAGIC) {
		logError(getPath(), " invalid file");
		return false;
	}
	if (header.version != 0) {
		logError(getPath(), " has unsupported version ", header.version);
		return false;
	}
	u32 is_surface;
	stream.read(is_surface);
	m_type = is_surface ? gpu::ShaderType::SURFACE : gpu::ShaderType::COMPUTE;

	const u32 num_uniforms = stream.read<u32>();
	m_uniforms.resize(num_uniforms);
	for (Uniform& u : m_uniforms) {
		copyString(u.name, stream.readString());
		u.name_hash = RuntimeHash(u.name, stringLength(u.name));
		stream.read(u.type);
		stream.read(u.offset);
		stream.read(u.default_value);
	}

	const u32 num_defines = stream.read<u32>();
	m_defines.resize(num_defines);
	for (u32 i = 0; i < num_defines; ++i) {
		const char* def = stream.readString();
		const u8 def_idx = m_renderer.getShaderDefineIdx(def);
		m_defines.push(def_idx);
	}

	const u32 num_texture_slots = stream.read<u32>();
	if (num_texture_slots >= lengthOf(m_texture_slots)) {
		logError(getPath(), " too many texture slots");
		return false;
	}
	m_texture_slot_count = num_texture_slots;
	for (u32 i = 0; i < num_texture_slots; ++i) {
		TextureSlot& slot = m_texture_slots[i];
		copyString(slot.name, stream.readString());

		const char* default_texture = stream.readString();
		ResourceManagerHub& manager = getResourceManager().getOwner();
		slot.default_texture = default_texture[0] ? manager.load<Texture>(Path(default_texture)) : nullptr;

		const char* define = stream.readString();
		if (define[0]) slot.define_idx = m_renderer.getShaderDefineIdx(define);
	}

	stream.read(m_content_hash);

	m_code = StringView((const char*)stream.skip(0), (u32)stream.remaining());

	return !stream.hasOverflow();
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
