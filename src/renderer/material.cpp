#include "renderer/material.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/hash.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "renderer/draw_stream.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "gpu/gpu.h"

namespace Lumix
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
	static u32 last_sort_key = 0;
	m_sort_key = ++last_sort_key;
	m_layer = m_renderer.getLayerIdx("default");
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

void Material::setLayer(u8 layer) { 
	if (m_layer == layer) return;
	m_layer = layer;
	refresh();
}

u32 Material::getCustomFlag(const char* flag_name)
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

	m_renderer.getEndFrameDrawStream().destroy(m_bind_group);
	m_bind_group = gpu::INVALID_BIND_GROUP;
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
	ASSERT(isReady());
	
	Span<const char> mat_dir = Path::getDir(getPath().c_str());
	if (startsWith(m_shader->getPath(), mat_dir)) {
		blob << "shader \"" << Span<const char>(m_shader->getPath()).fromLeft(mat_dir.length()) << "\"\n";
	}
	else {
		blob << "shader \"/" << m_shader->getPath().c_str() << "\"\n";
	}
	blob << "backface_culling(" << (isBackfaceCulling() ? "true" : "false") << ")\n";
	blob << "layer \"" << m_renderer.getLayerName(m_layer) << "\"\n";

	blob << "defines {";
	bool first_define = true;
	for (int i = 0; i < sizeof(m_define_mask) * 8; ++i) {
		if ((m_define_mask & (1 << i)) == 0) continue;
		const char* def = m_renderer.getShaderDefine(i);
		if (!first_define) blob << ", ";
		first_define = false;
		blob << "\"" << def << "\"";
	}
	blob << "}\n";

	for (u32 i = 0; i < m_texture_count; ++i) {
		char path[LUMIX_MAX_PATH];
		if (m_textures[i] && m_textures[i] != m_shader->m_texture_slots[i].default_texture) {
			Span<const char> texture_path(m_textures[i]->getPath());
			if (startsWith(texture_path, mat_dir)) {
				copyString(Span(path), Span(texture_path).fromLeft(mat_dir.length()));
				blob << "texture \"" << path << "\"\n";
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
	
	auto writeArray = [&blob](const float* value, u32 num) {
		blob << "{ ";
		for (u32 i = 0; i < num; ++i) {
			if (i > 0) blob << ", ";
			blob << value[i];
		}
		blob << " }";
	};

	for (const Shader::Uniform& su : m_shader->m_uniforms) {
		for (const Uniform& mu : m_uniforms) {
			if(mu.name_hash == su.name_hash) {
				if (su.type == Shader::Uniform::INT) {
					blob << "int_uniform(\"" << su.name << "\", " << mu.int_value << ")\n";
				}
				else {
					blob << "uniform(\"" << su.name << "\", ";
					switch(su.type) {
						case Shader::Uniform::INT: blob << mu.int_value; break;
						case Shader::Uniform::NORMALIZED_FLOAT: blob << mu.float_value; break;
						case Shader::Uniform::FLOAT: blob << mu.float_value; break;
						case Shader::Uniform::COLOR: 
						case Shader::Uniform::VEC4: writeArray(mu.vec4, 4); break;
						case Shader::Uniform::VEC3: writeArray(mu.vec4, 3); break;
						case Shader::Uniform::VEC2: writeArray(mu.vec4, 2); break;
					}
					blob << ")\n";
				}
				break;
			}
		}
	}
}


int Material::uniform(lua_State* L) {
	const char* name = LuaWrapper::checkArg<const char*>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	Uniform u;
	u.name_hash = RuntimeHash(name);
	switch (lua_type(L, 2)) {
		case LUA_TNUMBER: u.float_value = LuaWrapper::toType<float>(L, 2); break;
		case LUA_TTABLE: {
			const size_t len = lua_objlen(L, 2);
			switch (len) {
				case 2:	*(Vec2*)u.vec2 = LuaWrapper::toType<Vec2>(L, 2); break;
				case 3: *(Vec3*)u.vec3 = LuaWrapper::toType<Vec3>(L, 2); break;
				case 4: *(Vec4*)u.vec4 = LuaWrapper::toType<Vec4>(L, 2); break;
				default: luaL_error(L, "Uniform %s has unsupported type", name); break;
			}
			break;
		}
		default: luaL_error(L, "Uniform %s has unsupported type", name); break;
	}
	material->m_uniforms.push(u);
	return 0;
}


int Material::int_uniform(lua_State* L) {
	const char* name = LuaWrapper::checkArg<const char*>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	Uniform u;
	u.name_hash = RuntimeHash(name);
	switch (lua_type(L, 2)) {
		case LUA_TNUMBER: u.int_value = LuaWrapper::toType<i32>(L, 2); break;
		default: luaL_error(L, "Uniform %s has unsupported type", name); break;
	}
	material->m_uniforms.push(u);
	return 0;
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
	Shader* shader = m_resource_manager.getOwner().load<Shader>(path);
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

	updateRenderData(true);
}


void Material::updateRenderData(bool on_before_ready)
{
	if (!m_shader) return;
	if (!on_before_ready && !isReady()) return;

	if (m_material_constants) m_renderer.destroyMaterialConstants(m_material_constants);

	float cs[Material::MAX_UNIFORMS_FLOATS] = {};
	for (const Shader::Uniform& shader_uniform : m_shader->m_uniforms) {
		bool found = false;
		const u32 size = shader_uniform.size();
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

	m_material_constants = m_renderer.createMaterialConstants(Span(cs));

	DrawStream& stream = m_renderer.getDrawStream();
	if (m_bind_group) stream.destroy(m_bind_group);
	m_bind_group = gpu::allocBindGroupHandle();
	
	gpu::BindGroupEntryDesc descs[MAX_TEXTURE_COUNT + 1];
	for(u32 i = 0; i < m_texture_count; ++i) {
		descs[i].texture = m_textures[i] ? m_textures[i]->handle : gpu::INVALID_TEXTURE;
		descs[i].type = gpu::BindGroupEntryDesc::TEXTURE;
		descs[i].bind_point = i;
	}
	descs[m_texture_count].type = gpu::BindGroupEntryDesc::UNIFORM_BUFFER;
	descs[m_texture_count].buffer = m_renderer.getMaterialUniformBuffer();
	descs[m_texture_count].size = MAX_UNIFORMS_BYTES;
	descs[m_texture_count].offset = m_material_constants * MAX_UNIFORMS_BYTES;
	descs[m_texture_count].bind_point = UniformBuffer::MATERIAL;
	stream.createBindGroup(m_bind_group, Span(descs, m_texture_count + 1));
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


namespace LuaAPI
{


int layer(lua_State* L)
{
	const char* layer_name = LuaWrapper::checkArg<const char*>(L, 1);
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	const u8 layer = material->getRenderer().getLayerIdx(layer_name);
	material->setLayer(layer);
	return 0;
}


int roughness(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	logWarning(material->getPath().c_str(), ": roughness deprecated");
	return 0;
}


int alpha_ref(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	logWarning(material->getPath(), ": alpha_ref deprecated");
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
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	logWarning(material->getPath().c_str(), ": color deprecated");

	return 0;
}


int custom_flag(lua_State* L)
{
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
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	logWarning(material->getPath().c_str(), ": metallic deprecated");
	return 0;
}


int emission(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	logWarning(material->getPath().c_str(), ": emission deprecated");
	return 0;
}


int translucency(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	logWarning(material->getPath().c_str(), ": translucency deprecated");
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

	if (!path[0]) material->setShader(nullptr);
	else {
		char c = path[0];
		if (c != '\\' && c != '/') {
			Span <const char> material_dir = Path::getDir(material->getPath().c_str());
			StaticString<LUMIX_MAX_PATH> fullpath(material_dir, path);
			material->setShader(Path(fullpath));
		}
		else {
			material->setShader(Path(path));
		}
	}
	return 0;
}


int texture(lua_State* L)
{
	lua_getfield(L, LUA_GLOBALSINDEX, "this");
	Material* material = (Material*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	char material_dir[LUMIX_MAX_PATH];
	copyString(Span(material_dir), Path::getDir(material->getPath().c_str()));

	if (lua_istable(L, 1)) {
		lua_getfield(L, 1, "source");
		if (lua_isstring(L, -1)) {
			const char* path = lua_tostring(L, -1);
			const int idx = material->getTextureCount();
			
			char texture_path[LUMIX_MAX_PATH];
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
			logError(material->getPath(), " texture's source is not a string.");
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
	
	char texture_path[LUMIX_MAX_PATH];
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

bool Material::wireframe() const {
	return u32(m_render_states & gpu::StateFlags::WIREFRAME);
}

void Material::setWireframe(bool enable) {
	if (enable) m_render_states = m_render_states | gpu::StateFlags::WIREFRAME;
	else m_render_states = m_render_states & ~gpu::StateFlags::WIREFRAME;
}

bool Material::load(Span<const u8> mem) {
	PROFILE_FUNCTION();

	MaterialManager& mng = static_cast<MaterialManager&>(getResourceManager());
	lua_State* L = mng.getState(*this);
	
	m_uniforms.clear();
	m_render_states = gpu::StateFlags::CULL_BACK;
	m_custom_flags = 0;

	const Span<const char> content((const char*)mem.begin(), (u32)mem.length());
	if (!LuaWrapper::execute(L, content, getPath().c_str(), 0)) {
		return false;
	}

	if (!m_shader) {
		logError("Material ", getPath(), " does not have a shader.");
		return false;
	}

	return true;
}

lua_State* MaterialManager::getState(Material& material) const {
	lua_pushlightuserdata(m_state, &material);
	lua_setfield(m_state, LUA_GLOBALSINDEX, "this");
	return m_state;
}

MaterialManager::~MaterialManager() {
	lua_close(m_state);
}

MaterialManager::MaterialManager(Renderer& renderer, IAllocator& allocator)
	: ResourceManager(allocator)
	, m_renderer(renderer)
{
	m_state = luaL_newstate();

	#define DEFINE_LUA_FUNC(func) \
		lua_pushcfunction(m_state, LuaAPI::func); \
		lua_setfield(m_state, LUA_GLOBALSINDEX, #func); 
	
	DEFINE_LUA_FUNC(alpha_ref);
	DEFINE_LUA_FUNC(backface_culling);
	DEFINE_LUA_FUNC(color);
	DEFINE_LUA_FUNC(custom_flag);
	DEFINE_LUA_FUNC(defines);
	DEFINE_LUA_FUNC(emission);
	DEFINE_LUA_FUNC(translucency);
	DEFINE_LUA_FUNC(layer);
	DEFINE_LUA_FUNC(metallic);
	DEFINE_LUA_FUNC(roughness);
	DEFINE_LUA_FUNC(shader);
	DEFINE_LUA_FUNC(texture);

	lua_pushcfunction(m_state, &Material::uniform);
	lua_setfield(m_state, LUA_GLOBALSINDEX, "uniform"); 

	lua_pushcfunction(m_state, &Material::int_uniform);
	lua_setfield(m_state, LUA_GLOBALSINDEX, "int_uniform"); 

	#undef DEFINE_LUA_FUNC

}

Resource* MaterialManager::createResource(const Path& path) {
	return LUMIX_NEW(m_allocator, Material)(path, *this, m_renderer, m_allocator);
}

void MaterialManager::destroyResource(Resource& resource) {
	LUMIX_DELETE(m_allocator, &resource);
};

} // namespace Lumix
