#include "renderer/material.h"
#include "engine/crc32.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
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


Material::Material(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator)
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
	, m_render_data(nullptr)
{
	static u32 last_sort_key = 0;
	m_sort_key = ++last_sort_key;
	m_layer = m_renderer.getLayerIdx("default");
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
	ResourceManager* texture_manager = m_resource_manager.getOwner().get(Texture::TYPE);
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
	
	m_renderer.runInRenderThread(m_render_data, [](Renderer& renderer, void* ptr){
		LUMIX_DELETE(renderer.getAllocator(), (RenderData*)ptr);
	});
	m_render_data = nullptr;

	setShader(nullptr);

	m_alpha_ref = 0.3f;
	m_color.set(1, 1, 1, 1);
	m_custom_flags = 0;
	m_define_mask = 0;
	m_metallic = 0.0f;
	m_roughness = 1.0f;
	m_emission = 0.0f;
	m_render_states = u64(ffr::StateFlags::CULL_BACK);
}


bool Material::save(IOutputStream& file)
{
	if(!isReady()) return false;
	if(!m_shader) return false;
	
	file << "shader \"" << m_shader->getPath().c_str() << "\"\n";
	file << "backface_culling(" << (isBackfaceCulling() ? "true" : "false") << ")\n";
	file << "layer \"" << m_renderer.getLayerName(m_layer) << "\"\n";

	char tmp[64];
	toCString(m_metallic, tmp, lengthOf(tmp), 9);
	file << "metallic(" <<  tmp << ")\n";
	toCString(m_roughness, tmp, lengthOf(tmp), 9);
	file << "roughness(" <<  tmp << ")\n";

	file << "defines {";
	for (int i = 0; i < sizeof(m_define_mask) * 8; ++i) {
		if ((m_define_mask & (1 << i)) == 0) continue;
		const char* def = m_renderer.getShaderDefine(i);
		if (equalStrings("SKINNED", def)) continue;
		if (i > 0) file << ", ";
		file << "\"" << def << "\"";
	}
	file << "}\n";

	StaticString<1024> color_tmp("", m_color.x, ", ", m_color.y, ", ", m_color.z, ", ", m_color.w);
	file << "color { " << color_tmp << " }\n";

	for (int i = 0; i < m_texture_count; ++i) {
		char path[MAX_PATH_LENGTH];
		if (m_textures[i] && m_textures[i] != m_shader->m_texture_slots[i].default_texture) {
			copyString(path, MAX_PATH_LENGTH, m_textures[i]->getPath().c_str());
		}
		else {
			path[0] = '\0';
		}
		file << "texture \"/" << path << "\"\n";
		
			/*serializer.beginObject("texture");
		serializer.serialize("source", path);
		if (flags & BGFX_TEXTURE_U_CLAMP) serializer.serialize("u_clamp", true);
		if (flags & BGFX_TEXTURE_V_CLAMP) serializer.serialize("v_clamp", true);
		if (flags & BGFX_TEXTURE_W_CLAMP) serializer.serialize("w_clamp", true);
		if (flags & BGFX_TEXTURE_MIN_POINT) serializer.serialize("min_filter", "point");
		if (flags & BGFX_TEXTURE_MIN_ANISOTROPIC) serializer.serialize("min_filter", "anisotropic");
		if (flags & BGFX_TEXTURE_MAG_POINT) serializer.serialize("mag_filter", "point");
		if (flags & BGFX_TEXTURE_MAG_ANISOTROPIC) serializer.serialize("mag_filter", "anisotropic");
		if (m_textures[i] && m_textures[i]->getData()) serializer.serialize("keep_data", true);
		serializer.endObject();*/
	}

	// TODO
	/*

	serializer.beginObject();
	serializer.serialize("render_layer", renderer.getLayerName(m_render_layer));
	serializer.serialize("layers_count", m_layers_count);
	serializer.serialize("backface_culling", isBackfaceCulling());
	

	if (m_custom_flags != 0)
	{
		serializer.beginArray("custom_flags");
		for (int i = 0; i < 32; ++i)
		{
			if (m_custom_flags & (1 << i)) serializer.serializeArrayItem(s_custom_flags.flags[i]);
		}
		serializer.endArray();
	}

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
				logWarning("Renderer") << "Unknown label \"" << label << "\"";
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
		Texture* texture = m_resource_manager.getOwner().load<Texture>(path);
		setTexture(i, texture);
	}
}


void Material::setTexture(int i, Texture* texture)
{
	Texture* old_texture = i < m_texture_count ? m_textures[i] : nullptr;
	if (texture) addDependency(*texture);
	if (!texture && m_shader && m_shader->isReady() && m_shader->m_texture_slots[i].default_texture)
	{
		texture = m_shader->m_texture_slots[i].default_texture;
	}
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

	updateRenderData(false);
}


void Material::setShader(const Path& path)
{
	Shader* shader = m_resource_manager.getOwner().load<Shader>(path);
	setShader(shader);
}


void Material::onBeforeReady()
{
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

	for (int i = m_shader->m_texture_slot_count; i < m_texture_count; ++i) {
		setTexture(i, nullptr);
	}
	m_texture_count = minimum(m_texture_count, m_shader->m_texture_slot_count);

	updateRenderData(true);
}


void Material::updateRenderData(bool on_before_ready)
{
	if (!m_shader) return;
	if (!on_before_ready && !isReady()) return;

	if(m_render_data) {
		m_renderer.runInRenderThread(m_render_data, [](Renderer& renderer, void* ptr){
			LUMIX_DELETE(renderer.getAllocator(), (RenderData*)ptr);
		});
	}

	m_render_data = LUMIX_NEW(m_renderer.getAllocator(), RenderData);
	m_render_data->color = m_color;
	m_render_data->emission = m_emission;
	m_render_data->metallic = m_metallic;
	m_render_data->render_states = m_render_states;
	m_render_data->roughness = m_roughness;
	m_render_data->shader = m_shader->m_render_data;
	m_render_data->textures_count = m_texture_count;
	for(int i = 0; i < m_texture_count; ++i) {
		m_render_data->textures[i] = m_textures[i] ? m_textures[i]->handle : ffr::INVALID_TEXTURE;
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


int layer(lua_State* L)
{
	const char* layer_name = LuaWrapper::checkArg<const char*>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	const int layer = material->getRenderer().getLayerIdx(layer_name);
	material->setLayer(layer);
	return 0;
}


int roughness(lua_State* L)
{
	const float r = LuaWrapper::checkArg<float>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	material->setRoughness(r);
	return 0;
}


int alpha_ref(lua_State* L)
{
	const float r = LuaWrapper::checkArg<float>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	material->setAlphaRef(r);
	return 0;
}


int backface_culling(lua_State* L)
{
	const bool enable = LuaWrapper::checkArg<bool>(L, 1);
	
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	material->enableBackfaceCulling(enable);
	return 0;
}


int color(lua_State* L)
{
	const Vec4 c = LuaWrapper::checkArg<Vec4>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	material->setColor(c);
	return 0;
}


int custom_flag(lua_State* L)
{
	const float m = LuaWrapper::checkArg<float>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	const char* flag_name = LuaWrapper::checkArg<const char*>(L, 1);

	const u32 flag = material->getCustomFlag(flag_name);
	material->setCustomFlag(flag);

	return 0;
}


int metallic(lua_State* L)
{
	const float m = LuaWrapper::checkArg<float>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	material->setMetallic(m);
	return 0;
}


int emission(lua_State* L)
{
	const float m = LuaWrapper::checkArg<float>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	material->setEmission(m);
	return 0;
}


int defines(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	LuaWrapper::forEachArrayItem<const char*>(L, 1, "array of strings expected", [&](const char* v){
		material->setDefine(material->getRenderer().getShaderDefineIdx(v), true);
	});
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
			logError("Renderer") << material->getPath() << " texture's source is not a string.";
			lua_pop(L, 1);
			return 0;
		}
		lua_pop(L, 1);
		

		Texture* texture = material->getTexture(material->getTextureCount() - 1);
		bool keep_data = false;
		LuaWrapper::getOptionalField(L, 1, "keep_data", &keep_data);
		if (keep_data) texture->addDataReference();

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


bool Material::load(u64 size, const u8* mem)
{
	PROFILE_FUNCTION();

	// TODO reuse state
	lua_State* L = luaL_newstate();
	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_GLOBALSINDEX, "this");
	
	#define DEFINE_LUA_FUNC(func) \
		lua_pushcclosure(L, LuaAPI::func, 0); \
		lua_setfield(L, LUA_GLOBALSINDEX, #func); 
	
	DEFINE_LUA_FUNC(alpha_ref);
	DEFINE_LUA_FUNC(backface_culling);
	DEFINE_LUA_FUNC(color);
	DEFINE_LUA_FUNC(custom_flag);
	DEFINE_LUA_FUNC(defines);
	DEFINE_LUA_FUNC(emission);
	DEFINE_LUA_FUNC(layer);
	DEFINE_LUA_FUNC(metallic);
	DEFINE_LUA_FUNC(roughness);
	DEFINE_LUA_FUNC(shader);
	DEFINE_LUA_FUNC(texture);
	
	#undef DEFINE_LUA_FUNC

	setAlphaRef(DEFAULT_ALPHA_REF_VALUE);

	m_custom_flags = 0;

	const StringView content((const char*)mem, (int)size);
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
		else if (equalStrings(label, "render_layer"))
		{
		}
		else if (equalStrings(label, "uniforms"))
		{
			deserializeUniforms(serializer);
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
		else
		{
			logError("Renderer") << "Unknown parameter " << label << " in material " << getPath();
		}
	}
	serializer.deserializeObjectEnd();
	*/
	if (!m_shader)
	{
		logError("Renderer") << "Material " << getPath() << " without a shader";
		return false;
	}

	m_size = size;
	return true;
}


} // namespace Lumix
