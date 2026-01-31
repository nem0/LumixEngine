#include "core/hash.h"
#include "core/log.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/stream.h"
#include "core/tokenizer.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/resource_manager.h"
#include "gpu/gpu.h"
#include "renderer/draw_stream.h"
#include "renderer/material.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"

namespace black
{


static struct CustomFlags
{
	char flags[32][32];
	u32 count;
} s_custom_flags = {};


const ResourceType Material::TYPE("material");


Material::Material(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_shader(nullptr)
	, m_uniforms(allocator)
	, m_texture_count(0)
	, m_renderer(renderer)
	, m_render_states(gpu::StateFlags::CULL_BACK)
	, m_define_mask(0)
	, m_custom_flags(0)
{
	m_layer = m_renderer.getLayerIdx("default");
	for (int i = 0; i < MAX_TEXTURE_COUNT; ++i)
	{
		m_textures[i] = nullptr;
	}

	setShader(nullptr);
	u64 hash = u64(this);
}

const char* Material::getCustomFlagName(int index)
{
	return s_custom_flags.flags[index];
}


int Material::getCustomFlagCount()
{
	return s_custom_flags.count;
}

void Material::setLayer(u8 layer) { 
	if (m_layer == layer) return;
	m_layer = layer;
	refresh();
}

u32 Material::getCustomFlag(StringView flag_name)
{
	for (u32 i = 0; i < s_custom_flags.count; ++i)
	{
		if (equalStrings(s_custom_flags.flags[i], flag_name)) return 1 << i;
	}
	if (s_custom_flags.count >= lengthOf(s_custom_flags.flags))
	{
		ASSERT(false);
		return 0;
	}
	copyString(s_custom_flags.flags[s_custom_flags.count], flag_name);
	++s_custom_flags.count;
	return 1 << (s_custom_flags.count - 1);
}


bool Material::isDefined(u8 define_idx) const
{
	return (m_define_mask & (1 << define_idx)) != 0;
}


void Material::setDefine(u8 define_idx, bool enabled)
{
	u32 old_mask = m_define_mask;
	if (enabled) {
		m_define_mask |= 1 << define_idx;
	}
	else {
		m_define_mask &= ~(1 << define_idx);
	}
	if (old_mask != m_define_mask) updateRenderData(false);
}

Material::Uniform* Material::findUniform(RuntimeHash name_hash) {
	for (Uniform& u : m_uniforms) {
		if (u.name_hash == name_hash) return &u;
	}
	return nullptr;
}

void Material::unload()
{
	m_uniforms.clear();
	for (u32 i = 0; i < m_texture_count; i++) {
		if (m_textures[i]) {
			removeDependency(*m_textures[i]);
			m_textures[i]->decRefCount();
		}
	}
	m_texture_count = 0;

	for (Texture*& tex : m_textures) tex = nullptr;
	
	setShader(nullptr);

	m_custom_flags = 0;
	m_define_mask = 0;
	m_render_states = gpu::StateFlags::CULL_BACK;
}

void Material::deserialize(InputMemoryStream& blob) {
	unload();
	bool res = load(Span((const u8*)blob.getData(), (u32)blob.size()));
	ASSERT(res);
}

void Material::serialize(OutputMemoryStream& blob) {
	StringView mat_dir = Path::getDir(getPath());
	StringView shader_path = m_shader ? m_shader->getPath() : StringView("");
	if (startsWith(shader_path, mat_dir)) {
		shader_path.removePrefix(mat_dir.size());
		blob << "shader \"" << shader_path << "\"\n";
	}
	else {
		blob << "shader \"/" << shader_path << "\"\n";
	}
	blob << "backface_culling " << (isBackfaceCulling() ? "true" : "false") << "\n";
	blob << "layer \"" << m_renderer.getLayerName(m_layer) << "\"\n";

	for (int i = 0; i < sizeof(m_define_mask) * 8; ++i) {
		if ((m_define_mask & (1 << i)) == 0) continue;
		const char* def = m_renderer.getShaderDefine(i);
		blob << "define \"" << def << "\"\n";
	}

	for (u32 i = 0; i < m_texture_count; ++i) {
		if (m_textures[i] && m_textures[i] != m_shader->m_texture_slots[i].default_texture) {
			StringView texture_path = m_textures[i]->getPath();
			if (startsWith(texture_path, mat_dir)) {
				texture_path.removePrefix(mat_dir.size());
				blob << "texture \"" << texture_path << "\"\n";
			}
			else {
				blob << "texture \"/" << texture_path << "\"\n";
			}
		}
		else {
			blob << "texture \"\"\n";
		}
	}

	if (m_custom_flags != 0) {
		for (int i = 0; i < 32; ++i)
		{
			if (m_custom_flags & (1 << i)) {
				blob << "custom_flag \"" << s_custom_flags.flags[i] << "\"\n";
			}
		}
	}
	
	auto writeObject = [&blob](const float* value, u32 num) {
		blob << "{ ";
		for (u32 i = 0; i < num; ++i) {
			if (i > 0) blob << ", ";
			blob << value[i];
		}
		blob << " }";
	};

	if (m_shader) {
		for (const Shader::Uniform& su : m_shader->m_uniforms) {
			for (const Uniform& mu : m_uniforms) {
				if(mu.name_hash == su.name_hash) {
					if (su.type == Shader::Uniform::INT) {
						blob << "int_uniform \"" << su.name << "\", " << mu.int_value << "\n";
					}
					else {
						blob << "uniform \"" << su.name << "\", ";
						switch(su.type) {
							case Shader::Uniform::INT: blob << mu.int_value; break;
							case Shader::Uniform::NORMALIZED_FLOAT: blob << mu.float_value; break;
							case Shader::Uniform::FLOAT: blob << mu.float_value; break;
							case Shader::Uniform::COLOR: 
							case Shader::Uniform::FLOAT4: writeObject(mu.vec4, 4); break;
							case Shader::Uniform::FLOAT3: writeObject(mu.vec4, 3); break;
							case Shader::Uniform::FLOAT2: writeObject(mu.vec4, 2); break;
						}
						blob << "\n";
					}
					break;
				}
			}
		}
	}
}

void Material::setTexturePath(int i, const Path& path)
{
	if (path.length() == 0)
	{
		setTexture(i, nullptr);
	}
	else
	{
		Texture* texture = m_resource_manager.getOwner().load<Texture>(path);
		setTexture(i, texture);
	}
}


void Material::setTexture(u32 i, Texture* texture)
{
	Texture* old_texture = i < m_texture_count ? m_textures[i] : nullptr;
	if (!texture && m_shader && m_shader->isReady() && m_shader->m_texture_slots[i].default_texture)
	{
		texture = m_shader->m_texture_slots[i].default_texture;
		ASSERT(texture->wantReady());
		texture->incRefCount();
	}
	if (texture) addDependency(*texture);
	m_textures[i] = texture;
	if (i >= m_texture_count) m_texture_count = i + 1;

	if (old_texture) {
		removeDependency(*old_texture);
		old_texture->decRefCount();
	}
	if (isReady() && m_shader)
	{
		int define_idx = m_shader->m_texture_slots[i].define_idx;
		if(define_idx >= 0)
		{
			if(m_textures[i])
			{
				m_define_mask |= 1 << define_idx;
			}
			else
			{
				m_define_mask &= ~(1 << define_idx);
			}
		}
	}

	updateRenderData(false);
}


void Material::setShader(const Path& path)
{
	Shader* shader = path.isEmpty() ? nullptr : m_resource_manager.getOwner().load<Shader>(path);
	setShader(shader);
}


void Material::onBeforeReady()
{
	if (!m_shader) return;

	for (u32 i = 0; i < m_shader->m_texture_slot_count; ++i) {
		if (!m_textures[i] && m_shader->m_texture_slots[i].default_texture) {
			m_textures[i] = m_shader->m_texture_slots[i].default_texture;
			if (i >= m_texture_count) m_texture_count = i + 1;
			ASSERT(m_textures[i]->wantReady());
			m_textures[i]->incRefCount();
			addDependency(*m_textures[i]);
			return;
		}
	}

	for (u32 i = 0; i < m_shader->m_texture_slot_count; ++i) {
		const int define_idx = m_shader->m_texture_slots[i].define_idx;
		if (define_idx >= 0) {
			if (m_textures[i]) {
				m_define_mask |= 1 << define_idx;
			} else {
				m_define_mask &= ~(1 << define_idx);
			}
		}
	}

	for (u32 i = m_shader->m_texture_slot_count; i < m_texture_count; ++i) {
		setTexture(i, nullptr);
	}
	m_texture_count = minimum(m_texture_count, m_shader->m_texture_slot_count);

	RollingHasher hasher;
	hasher.begin();
	hasher.update(&m_shader, sizeof(m_shader));
	hasher.update(&m_define_mask, sizeof(m_define_mask));
	hasher.update(&m_render_states, sizeof(m_render_states));
	RuntimeHash32 hash = hasher.end();
	m_sort_key = hash.getHashValue();
	updateRenderData(true);
}

void Material::getUniformData(Span<float> data) const {
	if (!m_shader) return;

	// TODO check overflow
	u8* cs = (u8*)data.begin();
	u32 textures_offset = 0;
	for (const Shader::Uniform& shader_uniform : m_shader->m_uniforms) {
		const u32 size = shader_uniform.size();
		textures_offset = maximum(textures_offset, shader_uniform.offset + size);
		bool found = false;
		for (Uniform& uniform : m_uniforms) {
			if (shader_uniform.name_hash == uniform.name_hash) {
				memcpy((u8*)cs + shader_uniform.offset, uniform.vec4, size);
				found = true;
				break;
			}
		}
		if (!found) {
			memcpy((u8*)cs + shader_uniform.offset, shader_uniform.default_value.vec4, size);
		}
	}
	for (u32 i = 0; i < m_shader->m_texture_slot_count; ++i) {
		const gpu::BindlessHandle bindless_handle = m_textures[i] ? gpu::getBindlessHandle(m_textures[i]->handle) : gpu::BindlessHandle();
		memcpy((u8*)cs + textures_offset + i * sizeof(bindless_handle), &bindless_handle, sizeof(bindless_handle));
	}
}

void Material::updateRenderData(bool on_before_ready)
{
	if (!m_shader) return;
	if (!on_before_ready && !isReady()) return;

	m_renderer.destroyMaterialConstants(m_material_constants);

	// TODO check overflow
	float cs[Material::MAX_UNIFORMS_FLOATS] = {};
	u32 textures_offset = 0;
	for (const Shader::Uniform& shader_uniform : m_shader->m_uniforms) {
		const u32 size = shader_uniform.size();
		textures_offset = maximum(textures_offset, shader_uniform.offset + size);
		bool found = false;
		for (Uniform& uniform : m_uniforms) {
			if (shader_uniform.name_hash == uniform.name_hash) {
				memcpy((u8*)cs + shader_uniform.offset, uniform.vec4, size);
				found = true;
				break;
			}
		}
		if (!found) {
			memcpy((u8*)cs + shader_uniform.offset, shader_uniform.default_value.vec4, size);
		}
	}
	for (u32 i = 0; i < m_shader->m_texture_slot_count; ++i) {
		const gpu::BindlessHandle bindless_handle = m_textures[i] ? gpu::getBindlessHandle(m_textures[i]->handle) : gpu::BindlessHandle();
		memcpy((u8*)cs + textures_offset + i * sizeof(bindless_handle), &bindless_handle, sizeof(bindless_handle));
	}

	m_material_constants = m_renderer.createMaterialConstants(Span(cs));
}

void Material::setShader(Shader* shader)
{
	if (m_shader) {
		Shader* shader = m_shader;
		m_shader = nullptr;
		removeDependency(*shader);
		shader->decRefCount();
	}
	m_shader = shader;
	if (m_shader) {
		addDependency(*m_shader);
	}

	updateRenderData(false);
}


Texture* Material::getTextureByName(const char* name) const
{
	if (!m_shader) return nullptr;

	for (int i = 0, c = m_shader->m_texture_slot_count; i < c; ++i) {
		if (equalStrings(m_shader->m_texture_slots[i].name, name)) {
			return m_textures[i];
		}
	}
	return nullptr;
}


bool Material::isTextureDefine(u8 define_idx) const
{
	if (!m_shader) return false;

	for (int i = 0, c = m_shader->m_texture_slot_count; i < c; ++i) {
		if (m_shader->m_texture_slots[i].define_idx == define_idx) {
			return true;
		}
	}
	return false;
}


void Material::enableBackfaceCulling(bool enable)
{
	if (enable) {
		m_render_states = m_render_states | gpu::StateFlags::CULL_BACK;
	}
	else {
		m_render_states = m_render_states & ~gpu::StateFlags::CULL_BACK;
	}
}

bool Material::isBackfaceCulling() const
{
	return u64(m_render_states & gpu::StateFlags::CULL_BACK);
}

bool Material::wireframe() const {
	return u32(m_render_states & gpu::StateFlags::WIREFRAME);
}

void Material::setWireframe(bool enable) {
	if (enable) m_render_states = m_render_states | gpu::StateFlags::WIREFRAME;
	else m_render_states = m_render_states & ~gpu::StateFlags::WIREFRAME;
}

bool Material::load(Span<const u8> mem) {
	PROFILE_FUNCTION();

	m_uniforms.clear();
	m_render_states = gpu::StateFlags::CULL_BACK;
	m_custom_flags = 0;

	Tokenizer tokenizer(mem, getPath().c_str());

	for (;;) {
		Tokenizer::Token key = tokenizer.tryNextToken();
		switch (key.type) {
			case Tokenizer::Token::ERROR: return false;
			case Tokenizer::Token::EOF: return true;
			default: break;
		}
		
		StringView value;

		if (key == "shader") {
			if (!tokenizer.consume(value)) return false;
			// TODO does not Path::normalize already do this?
			if (startsWith(value, "/") || startsWith(value, "\\")) value.removePrefix(1);
			setShader(Path(value));
		}
		else if (key == "custom_flag") {
			if (!tokenizer.consume(value)) return false;
			const u32 flag = getCustomFlag(value);
			setCustomFlag(flag);
		}
		else if (key == "define") {
			char define_str[32];
			if (!tokenizer.consume(define_str)) return false;
			const u8 define_idx = getRenderer().getShaderDefineIdx(define_str);
			setDefine(define_idx, true);
		}
		else if (key == "layer") {
			char layer_name[64];
			if (!tokenizer.consume(layer_name)) return false;
			const u8 layer = getRenderer().getLayerIdx(layer_name);
			setLayer(layer);
		}
		else if (key == "texture") {
			if (!tokenizer.consume(value)) return false;
			const i32 idx = getTextureCount();
			if (value.empty()) {
				setTexture(idx, nullptr);
			}
			else if (value[0] == '/' || value[0] == '\\') {
				value.removePrefix(1);
				setTexturePath(idx, Path(value));
			}
			else {
				StringView material_dir = Path::getDir(getPath());
				Path path(material_dir, value);
				setTexturePath(idx, path);
			}
		}
		else if (key == "backface_culling") {
			bool b;
			if (!tokenizer.consume(b)) return false;
			enableBackfaceCulling(b);
		}
		else if (key == "int_uniform") {
			StringView name;
			Uniform u;
			if (!tokenizer.consume(name, ",", u.int_value)) return false;
			u.name_hash = RuntimeHash(name.begin, name.size());
			m_uniforms.push(u);
		}
		else if (key == "uniform") {
			StringView name;
			if (!tokenizer.consume(name, ",")) return false;

			Uniform u;
			u.name_hash = RuntimeHash(name.begin, name.size());

			Tokenizer::Token token = tokenizer.nextToken();
			if (!token) return false;

			if (token.value[0] == '{') {
				u32 vecsize;
				if (!tokenizer.consumeVector(u.vec4, vecsize)) return false;
			}
			else if (token.type == Tokenizer::Token::NUMBER) {
				u.float_value = Tokenizer::toFloat(token);
			}
			m_uniforms.push(u);
		}
		else {
			logError(getPath(), "(", tokenizer.getLine(), "): Unexpected token ", key);
			tokenizer.logErrorPosition(key.value.begin);
			return false;
		}
	}

	if (!m_shader) {
		logError("Material ", getPath(), " does not have a shader.");
		return false;
	}

	return true;
}

} // namespace black
