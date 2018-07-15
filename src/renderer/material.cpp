#include "renderer/material.h"
#include "engine/crc32.h"
#include "engine/fs/file_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "renderer/material_manager.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "ffr/ffr.h"

namespace Lumix
{


static const float DEFAULT_ALPHA_REF_VALUE = 0.3f;


static struct CustomFlags
{
	char flags[32][32];
	int count;
} s_custom_flags = {};


const ResourceType Material::TYPE("material");


Material::Material(const Path& path, ResourceManagerBase& resource_manager, Renderer& renderer, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_shader(nullptr)
	, m_uniforms(allocator)
	, m_allocator(allocator)
	, m_texture_count(0)
	, m_renderer(renderer)
	, m_render_states(u64(ffr::StateFlags::CULL_BACK))
	, m_color(1, 1, 1, 1)
	, m_metallic(0)
	, m_roughness(1.0f)
	, m_emission(0.0f)
	, m_define_mask(0)
	, m_custom_flags(0)
	, m_render_layer(0)
	, m_render_layer_mask(1)
	, m_layers_count(0)
{
	setAlphaRef(DEFAULT_ALPHA_REF_VALUE);
	for (int i = 0; i < MAX_TEXTURE_COUNT; ++i)
	{
		m_textures[i] = nullptr;
	}

	setShader(nullptr);
}


Material::~Material()
{
	ASSERT(isEmpty());
}


const char* Material::getCustomFlagName(int index)
{
	return s_custom_flags.flags[index];
}


int Material::getCustomFlagCount()
{
	return s_custom_flags.count;
}


u32 Material::getCustomFlag(const char* flag_name)
{
	for (int i = 0; i < s_custom_flags.count; ++i)
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
	if (enabled) {
		m_define_mask |= 1 << define_idx;
	}
	else {
		m_define_mask &= ~(1 << define_idx);
	}
}


void Material::unload()
{
	// TODO
/*
	m_uniforms.clear();
	*/
	ResourceManagerBase* texture_manager = m_resource_manager.getOwner().get(Texture::TYPE);
	for (int i = 0; i < m_texture_count; i++) {
		if (m_textures[i] && (!m_shader || m_textures[i] != m_shader->m_texture_slots[i].default_texture)) {
			removeDependency(*m_textures[i]);
			texture_manager->unload(*m_textures[i]);
		}
	}
	m_texture_count = 0;
	for(Texture*& tex : m_textures ) {
		tex = nullptr;
	}
	
	setShader(nullptr);

	m_alpha_ref = 0.3f;
	m_color.set(1, 1, 1, 1);
	m_custom_flags = 0;
	m_define_mask = 0;
	m_layers_count = 0;
	m_metallic = 0.0f;
	m_render_layer = 0;
	m_render_layer_mask = 1;
	m_roughness = 1.0f;
	m_emission = 0.0f;
	m_render_states = u64(ffr::StateFlags::CULL_BACK);
}


bool Material::save(OutputBlob& blob)
{
		// TODO
	ASSERT(false);
/*
	if(!isReady()) return false;
	if(!m_shader) return false;

	auto& renderer = static_cast<MaterialManager&>(m_resource_manager).getRenderer();

	serializer.beginObject();
	serializer.serialize("render_layer", renderer.getLayerName(m_render_layer));
	serializer.serialize("layers_count", m_layers_count);
	serializer.serialize("shader", m_shader ? m_shader->getPath() : Path(""));
	serializer.serialize("backface_culling", isBackfaceCulling());
	for (int i = 0; i < m_texture_count; ++i)
	{
		char path[MAX_PATH_LENGTH];
		int flags = 0;
		if (m_textures[i] && m_textures[i] != m_shader->m_texture_slots[i].default_texture)
		{
			flags = m_textures[i]->bgfx_flags;
			path[0] = '/';
			copyString(path + 1, MAX_PATH_LENGTH - 1, m_textures[i]->getPath().c_str());
		}
		else
		{
			path[0] = '\0';
		}
		serializer.beginObject("texture");
		serializer.serialize("source", path);
		if (flags & BGFX_TEXTURE_SRGB) serializer.serialize("srgb", true);
		if (flags & BGFX_TEXTURE_U_CLAMP) serializer.serialize("u_clamp", true);
		if (flags & BGFX_TEXTURE_V_CLAMP) serializer.serialize("v_clamp", true);
		if (flags & BGFX_TEXTURE_W_CLAMP) serializer.serialize("w_clamp", true);
		if (flags & BGFX_TEXTURE_MIN_POINT) serializer.serialize("min_filter", "point");
		if (flags & BGFX_TEXTURE_MIN_ANISOTROPIC) serializer.serialize("min_filter", "anisotropic");
		if (flags & BGFX_TEXTURE_MAG_POINT) serializer.serialize("mag_filter", "point");
		if (flags & BGFX_TEXTURE_MAG_ANISOTROPIC) serializer.serialize("mag_filter", "anisotropic");
		if (m_textures[i] && m_textures[i]->getData()) serializer.serialize("keep_data", true);
		serializer.endObject();
	}

	if (m_custom_flags != 0)
	{
		serializer.beginArray("custom_flags");
		for (int i = 0; i < 32; ++i)
		{
			if (m_custom_flags & (1 << i)) serializer.serializeArrayItem(s_custom_flags.flags[i]);
		}
		serializer.endArray();
	}

	serializer.beginArray("defines");
	for (int i = 0; i < sizeof(m_define_mask) * 8; ++i)
	{
		if ((m_define_mask & (1 << i)) == 0) continue;
		const char* def = renderer.getShaderDefine(i);
		if (equalStrings("SKINNED", def)) continue;
		serializer.serializeArrayItem(def);
	}
	serializer.endArray();

	serializer.beginArray("uniforms");
	for (int i = 0; i < m_shader->m_uniforms.size(); ++i)
	{
		serializer.beginObject();
		const auto& uniform = m_shader->m_uniforms[i];

		serializer.serialize("name", uniform.name);
		switch (uniform.type)
		{
			case Shader::Uniform::FLOAT:
				serializer.serialize("float_value", m_uniforms[i].float_value);
				break;
			case Shader::Uniform::COLOR:
				serializer.beginArray("color");
					serializer.serializeArrayItem(m_uniforms[i].vec3[0]);
					serializer.serializeArrayItem(m_uniforms[i].vec3[1]);
					serializer.serializeArrayItem(m_uniforms[i].vec3[2]);
				serializer.endArray();
				break;
			case Shader::Uniform::VEC3:
				serializer.beginArray("vec3");
				serializer.serializeArrayItem(m_uniforms[i].vec3[0]);
				serializer.serializeArrayItem(m_uniforms[i].vec3[1]);
				serializer.serializeArrayItem(m_uniforms[i].vec3[2]);
				serializer.endArray();
				break;
			case Shader::Uniform::VEC4:
				serializer.beginArray("vec4");
				serializer.serializeArrayItem(m_uniforms[i].vec4[0]);
				serializer.serializeArrayItem(m_uniforms[i].vec4[1]);
				serializer.serializeArrayItem(m_uniforms[i].vec4[2]);
				serializer.serializeArrayItem(m_uniforms[i].vec4[3]);
				serializer.endArray();
				break;
			case Shader::Uniform::VEC2:
				serializer.beginArray("vec2");
				serializer.serializeArrayItem(m_uniforms[i].vec2[0]);
				serializer.serializeArrayItem(m_uniforms[i].vec2[1]);
				serializer.endArray();
				break;
			case Shader::Uniform::TIME:
				serializer.serialize("time", 0);
				break;
			case Shader::Uniform::INT:
				serializer.serialize("int_value", m_uniforms[i].int_value);
				break;
			case Shader::Uniform::MATRIX4:
				serializer.beginArray("matrix_value");
				for (int j = 0; j < 16; ++j)
				{
					serializer.serializeArrayItem(m_uniforms[i].matrix[j]);
				}
				serializer.endArray();
				break;
			default:
				ASSERT(false);
				break;
		}
		serializer.endObject();
	}
	serializer.endArray();
	serializer.serialize("metallic", m_metallic);
	serializer.serialize("roughness", m_roughness);
	serializer.serialize("emission", m_emission);
	serializer.serialize("alpha_ref", m_alpha_ref);
	serializer.beginArray("color");
		serializer.serializeArrayItem(m_color.x);
		serializer.serializeArrayItem(m_color.y);
		serializer.serializeArrayItem(m_color.z);
		serializer.serializeArrayItem(m_color.w);
	serializer.endArray();
	serializer.endObject();*/
	return true;
}


void Material::deserializeCustomFlags(lua_State* L)
{
		// TODO
	ASSERT(false);
/*
	m_custom_flags = 0;
	serializer.deserializeArrayBegin();
	while (!serializer.isArrayEnd())
	{
		char tmp[32];
		serializer.deserializeArrayItem(tmp, lengthOf(tmp), "");
		setCustomFlag(getCustomFlag(tmp));
	}
	serializer.deserializeArrayEnd();*/
}


void Material::deserializeUniforms(lua_State* L)
{
			// TODO
	ASSERT(false);
/*
serializer.deserializeArrayBegin();
	m_uniforms.clear();
	while (!serializer.isArrayEnd())
	{
		Uniform& uniform = m_uniforms.emplace();
		serializer.nextArrayItem();
		serializer.deserializeObjectBegin();
		char label[256];
		while (!serializer.isObjectEnd())
		{
			serializer.deserializeLabel(label, 255);
			if (equalStrings(label, "name"))
			{
				char name[32];
				serializer.deserialize(name, lengthOf(name), "");
				uniform.name_hash = crc32(name);
			}
			else if (equalStrings(label, "int_value"))
			{
				serializer.deserialize(uniform.int_value, 0);
			}
			else if (equalStrings(label, "float_value"))
			{
				serializer.deserialize(uniform.float_value, 0);
			}
			else if (equalStrings(label, "matrix_value"))
			{
				serializer.deserializeArrayBegin();
				for (int i = 0; i < 16; ++i)
				{
					serializer.deserializeArrayItem(uniform.matrix[i], 0);
				}
				serializer.deserializeArrayEnd();
			}
			else if (equalStrings(label, "time"))
			{
				serializer.deserialize(uniform.float_value, 0);
			}
			else if (equalStrings(label, "color"))
			{
				serializer.deserializeArrayBegin();
					serializer.deserializeArrayItem(uniform.vec3[0], 0);
					serializer.deserializeArrayItem(uniform.vec3[1], 0);
					serializer.deserializeArrayItem(uniform.vec3[2], 0);
				serializer.deserializeArrayEnd();
			}
			else if (equalStrings(label, "vec3"))
			{
				serializer.deserializeArrayBegin();
				serializer.deserializeArrayItem(uniform.vec3[0], 0);
				serializer.deserializeArrayItem(uniform.vec3[1], 0);
				serializer.deserializeArrayItem(uniform.vec3[2], 0);
				serializer.deserializeArrayEnd();
			}
			else if (equalStrings(label, "vec2"))
			{
				serializer.deserializeArrayBegin();
				serializer.deserializeArrayItem(uniform.vec2[0], 0);
				serializer.deserializeArrayItem(uniform.vec2[1], 0);
				serializer.deserializeArrayEnd();
			}
			else
			{
				g_log_warning.log("Renderer") << "Unknown label \"" << label << "\"";
			}
		}
		serializer.deserializeObjectEnd();
	}
	serializer.deserializeArrayEnd();*/
}


void Material::setTexturePath(int i, const Path& path)
{
	if (path.length() == 0)
	{
		setTexture(i, nullptr);
	}
	else
	{
		Texture* texture = static_cast<Texture*>(m_resource_manager.getOwner().get(Texture::TYPE)->load(path));
		setTexture(i, texture);
	}
}


void Material::setLayersCount(int layers)
{
	++m_empty_dep_count;
	checkState();
	m_layers_count = layers;
	--m_empty_dep_count;
	checkState();
}


void Material::setRenderLayer(int layer)
{
	++m_empty_dep_count;
	checkState();
	m_render_layer = layer;
	m_render_layer_mask = 1ULL << (u64)layer;
	--m_empty_dep_count;
	checkState();
}


void Material::setTexture(int i, Texture* texture)
{
	Texture* old_texture = i < m_texture_count ? m_textures[i] : nullptr;
	if (!texture && m_shader && m_shader->m_texture_slots[i].default_texture)
	{
		texture = m_shader->m_texture_slots[i].default_texture;
	}
	if (texture) addDependency(*texture);
	m_textures[i] = texture;
	if (i >= m_texture_count) m_texture_count = i + 1;

	if (old_texture && (!m_shader || old_texture != m_shader->m_texture_slots[i].default_texture))
	{
		removeDependency(*old_texture);
		m_resource_manager.getOwner().get(Texture::TYPE)->unload(*old_texture);
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
}


void Material::setShader(const Path& path)
{
	Shader* shader = static_cast<Shader*>(m_resource_manager.getOwner().get(Shader::TYPE)->load(path));
	setShader(shader);
}


void Material::onBeforeReady()
{
	// TODO
/*
	if (!m_shader) return;

	for(int i = 0; i < m_shader->m_uniforms.size(); ++i)
	{
		auto& shader_uniform = m_shader->m_uniforms[i];
		bool found = false;
		for(int j = i; j < m_uniforms.size(); ++j)
		{
			if(m_uniforms[j].name_hash == shader_uniform.name_hash)
			{
				auto tmp = m_uniforms[i];
				m_uniforms[i] = m_uniforms[j];
				m_uniforms[j] = tmp;
				found = true;
				break;
			}
		}
		if(found) continue;
		if(i < m_uniforms.size())
		{
			m_uniforms.emplace(m_uniforms[i]);
		}
		else
		{
			m_uniforms.emplace();
		}
		m_uniforms[i].name_hash = shader_uniform.name_hash;
	}

	u8 alpha_ref = u8(m_alpha_ref * 255.0f);
	m_render_states = (m_render_states & ~BGFX_STATE_ALPHA_REF_MASK) | BGFX_STATE_ALPHA_REF(alpha_ref);
	m_render_states |= m_shader->m_render_states;
	*/
	for(int i = 0; i < m_shader->m_texture_slot_count; ++i) {
		if (!m_textures[i] && m_shader->m_texture_slots[i].default_texture) {
			m_textures[i] = m_shader->m_texture_slots[i].default_texture;
			if (i >= m_texture_count) m_texture_count = i + 1;
		}
		const int define_idx = m_shader->m_texture_slots[i].define_idx;
		if(define_idx >= 0) {
			if(m_textures[i]) {
				m_define_mask |= 1 << define_idx;
			}
			else {
				m_define_mask &= ~(1 << define_idx);
			}
		}
	}
}


void Material::setShader(Shader* shader)
{
	if (m_shader) {
		Shader* shader = m_shader;
		m_shader = nullptr;
		removeDependency(*shader);
		m_resource_manager.getOwner().get(Shader::TYPE)->unload(*shader);
	}
	m_shader = shader;
	if (m_shader) {
		addDependency(*m_shader);
	}
}


ffr::UniformHandle Material::getTextureUniform(int i) const
{
	if (i < m_shader->m_texture_slot_count) return m_shader->m_texture_slots[i].uniform_handle;
	return ffr::INVALID_UNIFORM;
}


Texture* Material::getTextureByUniform(const char* uniform) const
{
	if (!m_shader) return nullptr;

	for (int i = 0, c = m_shader->m_texture_slot_count; i < c; ++i) {
		if (equalStrings(m_shader->m_texture_slots[i].uniform, uniform)) {
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


void Material::setAlphaRef(float value)
{
	m_alpha_ref = value;
}


void Material::enableBackfaceCulling(bool enable)
{
	if (enable) {
		m_render_states |= (u64)ffr::StateFlags::CULL_BACK;
	}
	else {
		m_render_states &= ~(u64)ffr::StateFlags::CULL_BACK;
	}
}


bool Material::isBackfaceCulling() const
{
	return (m_render_states & (u64)ffr::StateFlags::CULL_BACK) != 0;
}


namespace LuaAPI
{


int defines(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	const size_t count = lua_objlen(L, 1);
	for(int i = 0; i < count; ++i) {
		lua_rawgeti(L, 1, i + 1);
		auto rype = lua_type(L, -1);
		if(!lua_isstring(L, -1)) {
			g_log_error.log("Renderer") << "Define must be string, material " << material->getPath();
			lua_pop(L, 1);
			return 0;
		}
		const char* define = lua_tostring(L, -1);
		material->setDefine(material->getRenderer().getShaderDefineIdx(define), true);
		lua_pop(L, 1);
	}
	return 0;
}


int shader(lua_State* L)
{
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	material->setShader(Path(path));
	return 0;
}


int texture(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	char material_dir[MAX_PATH_LENGTH];
	PathUtils::getDir(material_dir, MAX_PATH_LENGTH, material->getPath().c_str());

	if (lua_istable(L, 1)) {
		lua_getfield(L, 1, "source");
		if (lua_isstring(L, -1)) {
			const char* path = lua_tostring(L, -1);
			const int idx = material->getTextureCount();
			
			char texture_path[MAX_PATH_LENGTH];
			if (path[0] != '/' && path[0] != '\\' && path[0] != '\0')
			{
				copyString(texture_path, material_dir);
				catString(texture_path, path);
			}
			else
			{
				copyString(texture_path, path);
			}

			material->setTexturePath(idx, Path(texture_path));
		}
		else {
			g_log_error.log("Renderer") << material->getPath() << " texture's source is not a string.";
			lua_pop(L, 1);
			return 0;
		}
		lua_pop(L, 1);

		lua_getfield(L, 1, "srgb");
		if (lua_isboolean(L, -1)) {
			const bool srgb = lua_toboolean(L, -1);
			Texture* texture = material->getTexture(material->getTextureCount() - 1);
			texture->setSRGB(srgb);
		}
		else if (!lua_isnil(L, -1)) {
			g_log_error.log("Renderer") << material->getPath() << " texture's srgb flag is not a boolean.";
		}
		lua_pop(L, 1);

		return 0;
	}
	
	const char* path = LuaWrapper::checkArg<const char*>(L, 1);
	const int idx = material->getTextureCount();
	
	char texture_path[MAX_PATH_LENGTH];
	if (path[0] != '/' && path[0] != '\\' && path[0] != '\0')
	{
		copyString(texture_path, material_dir);
		catString(texture_path, path);
	}
	else
	{
		copyString(texture_path, path);
	}

	material->setTexturePath(idx, Path(texture_path));
	return 0;
}


} // namespace LuaAPI


bool Material::load(FS::IFile& file)
{
	PROFILE_FUNCTION();

	lua_State* L = luaL_newstate();
	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_GLOBALSINDEX, "this");
	lua_pushcclosure(L, LuaAPI::shader, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "shader");
	lua_pushcclosure(L, LuaAPI::texture, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "texture");
	lua_pushcclosure(L, LuaAPI::defines, 0);
	lua_setfield(L, LUA_GLOBALSINDEX, "defines");
	
	setAlphaRef(DEFAULT_ALPHA_REF_VALUE);

	const StringView content((const char*)file.getBuffer(), (int)file.size());
	if (!LuaWrapper::execute(L, content, getPath().c_str(), 0)) {
		lua_close(L);
		return false;
	}
	lua_close(L);
	return m_shader != nullptr;

	// TODO
	ASSERT(false);
	/*
	m_render_states = BGFX_STATE_CULL_CW;
	
	m_uniforms.clear();
	
	JsonDeserializer serializer(file, getPath(), m_allocator);
	serializer.deserializeObjectBegin();
	char label[256];
	char material_dir[MAX_PATH_LENGTH];
	PathUtils::getDir(material_dir, MAX_PATH_LENGTH, getPath().c_str());
	while (!serializer.isObjectEnd())
	{
		serializer.deserializeLabel(label, 255);
		if (equalStrings(label, "defines"))
		{
			deserializeDefines(serializer);
		}
		else if (equalStrings(label, "custom_flags"))
		{
			deserializeCustomFlags(serializer);
		}
		else if (equalStrings(label, "layers_count"))
		{
			serializer.deserialize(m_layers_count, 0);
		}
		else if (equalStrings(label, "render_layer"))
		{
			char tmp[32];
			auto& renderer = static_cast<MaterialManager&>(m_resource_manager).getRenderer();
			serializer.deserialize(tmp, lengthOf(tmp), "default");
			m_render_layer = renderer.getLayer(tmp);
			m_render_layer_mask = 1ULL << (u64)m_render_layer;
		}
		else if (equalStrings(label, "uniforms"))
		{
			deserializeUniforms(serializer);
		}
		else if (equalStrings(label, "texture"))
		{
			if (!deserializeTexture(serializer, material_dir))
			{
				return false;
			}
		}
		else if (equalStrings(label, "alpha_ref"))
		{
			serializer.deserialize(m_alpha_ref, 0.3f);
		}
		else if (equalStrings(label, "backface_culling"))
		{
			bool b = true;
			serializer.deserialize(b, true);
			if (b)
			{
				m_render_states |= BGFX_STATE_CULL_CW;
			}
			else
			{
				m_render_states &= ~BGFX_STATE_CULL_MASK;
			}
		}
		else if (equalStrings(label, "color"))
		{
			serializer.deserializeArrayBegin();
			serializer.deserializeArrayItem(m_color.x, 1.0f);
			serializer.deserializeArrayItem(m_color.y, 1.0f);
			serializer.deserializeArrayItem(m_color.z, 1.0f);
			if (!serializer.isArrayEnd())
			{
				serializer.deserializeArrayItem(m_color.w, 1.0f);
			}
			else
			{
				m_color.w = 1;
			}
			serializer.deserializeArrayEnd();
		}
		else if (equalStrings(label, "metallic"))
		{
			serializer.deserialize(m_metallic, 0.0f);
		}
		else if (equalStrings(label, "roughness"))
		{
			serializer.deserialize(m_roughness, 1.0f);
		}
		else if (equalStrings(label, "emission"))
		{
			serializer.deserialize(m_emission, 0.0f);
		}
		else if (equalStrings(label, "shader"))
		{
			Path path;
			serializer.deserialize(path, Path(""));
			auto* manager = m_resource_manager.getOwner().get(Shader::TYPE);
			setShader(static_cast<Shader*>(manager->load(Path(path))));
		}
		else
		{
			g_log_error.log("Renderer") << "Unknown parameter " << label << " in material " << getPath();
		}
	}
	serializer.deserializeObjectEnd();
	*/
	if (!m_shader)
	{
		g_log_error.log("Renderer") << "Material " << getPath() << " without a shader";
		return false;
	}

	m_size = file.size();
	return true;
}


} // namespace Lumix
