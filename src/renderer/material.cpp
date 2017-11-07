#include "renderer/material.h"
#include "engine/crc32.h"
#include "engine/fs/file_system.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/path_utils.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "renderer/material_manager.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"


namespace Lumix
{


static const u32 SHADOWMAP_HASH = crc32("shadowmap");
static const ResourceType TEXTURE_TYPE("texture");
static const ResourceType SHADER_TYPE("shader");
static const ResourceType MATERIAL_TYPE("material");
static const float DEFAULT_ALPHA_REF_VALUE = 0.3f;


static struct CustomFlags
{
	char flags[32][32];
	int count;
} s_custom_flags = {};


static u8 DEFAULT_COMMAND_BUFFER = 0;


Material::Material(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_shader(nullptr)
	, m_uniforms(allocator)
	, m_allocator(allocator)
	, m_texture_count(0)
	, m_render_states(BGFX_STATE_CULL_CW)
	, m_color(1, 1, 1, 1)
	, m_metallic(0)
	, m_roughness(1.0f)
	, m_shader_instance(nullptr)
	, m_define_mask(0)
	, m_command_buffer(&DEFAULT_COMMAND_BUFFER)
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


bool Material::hasDefine(u8 define_idx) const
{
	return m_shader->hasDefine(define_idx) != 0;
}


void Material::setDefine(u8 define_idx, bool enabled)
{
	u32 old_mask = m_define_mask;
	if (enabled)
	{
		m_define_mask |= 1 << define_idx;
	}
	else
	{
		m_define_mask &= ~(1 << define_idx);
	}

	if (!isReady()) return;
	if (!m_shader) return;

	if (old_mask != m_define_mask)
	{
		m_shader_instance = &m_shader->getInstance(m_define_mask);
	}
}


void Material::unload(void)
{
	if(m_command_buffer != &DEFAULT_COMMAND_BUFFER) m_allocator.deallocate(m_command_buffer);
	m_command_buffer = &DEFAULT_COMMAND_BUFFER;
	m_uniforms.clear();
	setShader(nullptr);

	ResourceManagerBase* texture_manager = m_resource_manager.getOwner().get(TEXTURE_TYPE);
	for (int i = 0; i < m_texture_count; i++)
	{
		if (m_textures[i])
		{
			removeDependency(*m_textures[i]);
			texture_manager->unload(*m_textures[i]);
		}
	}
	m_alpha_ref = 0.3f;
	m_color.set(1, 1, 1, 1);
	m_custom_flags = 0;
	m_define_mask = 0;
	m_layers_count = 0;
	m_metallic = 0.0f;
	m_render_layer = 0;
	m_render_layer_mask = 1;
	m_roughness = 1.0f;
	m_texture_count = 0;
}


bool Material::save(JsonSerializer& serializer)
{
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
		if (m_textures[i])
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
	serializer.serialize("alpha_ref", m_alpha_ref);
	serializer.beginArray("color");
		serializer.serializeArrayItem(m_color.x);
		serializer.serializeArrayItem(m_color.y);
		serializer.serializeArrayItem(m_color.z);
		serializer.serializeArrayItem(m_color.w);
	serializer.endArray();
	serializer.endObject();
	return true;
}


void Material::deserializeCustomFlags(JsonSerializer& serializer)
{
	m_custom_flags = 0;
	serializer.deserializeArrayBegin();
	while (!serializer.isArrayEnd())
	{
		char tmp[32];
		serializer.deserializeArrayItem(tmp, lengthOf(tmp), "");
		setCustomFlag(getCustomFlag(tmp));
	}
	serializer.deserializeArrayEnd();
}


void Material::deserializeDefines(JsonSerializer& serializer)
{
	auto& renderer = static_cast<MaterialManager&>(m_resource_manager).getRenderer();
	serializer.deserializeArrayBegin();
	m_define_mask = 0;
	while (!serializer.isArrayEnd())
	{
		char tmp[32];
		serializer.deserializeArrayItem(tmp, lengthOf(tmp), "");
		m_define_mask |= 1 << renderer.getShaderDefineIdx(tmp);
	}
	serializer.deserializeArrayEnd();
}


void Material::deserializeUniforms(JsonSerializer& serializer)
{
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
	serializer.deserializeArrayEnd();
}


void Material::setTexturePath(int i, const Path& path)
{
	if (path.length() == 0)
	{
		setTexture(i, nullptr);
	}
	else
	{
		Texture* texture = static_cast<Texture*>(m_resource_manager.getOwner().get(TEXTURE_TYPE)->load(path));
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

	if (texture) addDependency(*texture);
	m_textures[i] = texture;
	if (i >= m_texture_count) m_texture_count = i + 1;

	if (old_texture)
	{
		removeDependency(*old_texture);
		m_resource_manager.getOwner().get(TEXTURE_TYPE)->unload(*old_texture);
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

		createCommandBuffer();
		m_shader_instance = &m_shader->getInstance(m_define_mask);
	}
}


void Material::setShader(const Path& path)
{
	Shader* shader = static_cast<Shader*>(m_resource_manager.getOwner().get(SHADER_TYPE)->load(path));
	setShader(shader);
}


void Material::createCommandBuffer()
{
	if (m_command_buffer != &DEFAULT_COMMAND_BUFFER) m_allocator.deallocate(m_command_buffer);
	m_command_buffer = &DEFAULT_COMMAND_BUFFER;
	if (!m_shader) return;

	CommandBufferGenerator generator;

	for (int i = 0; i < m_shader->m_uniforms.size(); ++i)
	{
		const Material::Uniform& uniform = m_uniforms[i];
		const Shader::Uniform& shader_uniform = m_shader->m_uniforms[i];

		switch (shader_uniform.type)
		{
			case Shader::Uniform::FLOAT:
				generator.setUniform(shader_uniform.handle, Vec4(uniform.float_value, 0, 0, 0));
				break;
			case Shader::Uniform::VEC2:
				generator.setUniform(shader_uniform.handle, Vec4(uniform.vec2[0], uniform.vec2[1], 0, 0));
				break;
			case Shader::Uniform::VEC3:
			case Shader::Uniform::COLOR:
				generator.setUniform(shader_uniform.handle, Vec4(*(Vec3*)uniform.vec3, 0));
				break;
			case Shader::Uniform::TIME: generator.setTimeUniform(shader_uniform.handle); break;
			default: ASSERT(false); break;
		}
	}

	for (int i = 0; i < m_shader->m_texture_slot_count; ++i)
	{
		if (i >= m_texture_count || !m_textures[i]) continue;

		generator.setTexture(i, m_shader->m_texture_slots[i].uniform_handle, m_textures[i]->handle);
	}

	auto& renderer = static_cast<MaterialManager&>(m_resource_manager).getRenderer();
	auto& uniform = renderer.getMaterialColorUniform();
	generator.setUniform(uniform, m_color);

	Vec4 roughness_metallic(m_roughness, m_metallic, 0, 0);
	auto& rm_uniform = renderer.getRoughnessMetallicUniform();
	generator.setUniform(rm_uniform, roughness_metallic);
	generator.end();

	m_command_buffer = (u8*)m_allocator.allocate(generator.getSize());
	generator.getData(m_command_buffer);
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

	u8 alpha_ref = u8(m_alpha_ref * 255.0f);
	m_render_states = (m_render_states & ~BGFX_STATE_ALPHA_REF_MASK) | BGFX_STATE_ALPHA_REF(alpha_ref);
	m_render_states |= m_shader->m_render_states;

	for(int i = 0; i < m_shader->m_texture_slot_count; ++i)
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

	createCommandBuffer();
	m_shader_instance = &m_shader->getInstance(m_define_mask);
}


void Material::setShader(Shader* shader)
{
	auto& mat_manager = static_cast<MaterialManager&>(m_resource_manager);

	if (m_shader && m_shader != mat_manager.getRenderer().getDefaultShader())
	{
		Shader* shader = m_shader;
		m_shader = nullptr;
		removeDependency(*shader);
		m_resource_manager.getOwner().get(SHADER_TYPE)->unload(*shader);
	}
	m_shader = shader;
	if (m_shader)
	{
		addDependency(*m_shader);
		if (m_shader->isReady()) onBeforeReady();
	}
	else
	{
		m_shader = mat_manager.getRenderer().getDefaultShader();
		m_shader_instance = m_shader->m_instances.empty() ? nullptr : &m_shader->m_instances[0];
	}
}


const char* Material::getTextureUniform(int i)
{
	if (i < m_shader->m_texture_slot_count) return m_shader->m_texture_slots[i].uniform;
	return "";
}


Texture* Material::getTextureByUniform(const char* uniform) const
{
	if (!m_shader) return nullptr;

	for (int i = 0, c = m_shader->m_texture_slot_count; i < c; ++i)
	{
		if (equalStrings(m_shader->m_texture_slots[i].uniform, uniform))
		{
			return m_textures[i];
		}
	}
	return nullptr;
}


bool Material::isTextureDefine(u8 define_idx) const
{
	if (!m_shader) return false;

	for (int i = 0, c = m_shader->m_texture_slot_count; i < c; ++i)
	{
		if (m_shader->m_texture_slots[i].define_idx == define_idx)
		{
			return true;
		}
	}
	return false;
}

bool Material::deserializeTexture(JsonSerializer& serializer, const char* material_dir)
{
	char path[MAX_PATH_LENGTH];
	serializer.deserializeObjectBegin();
	char label[256];
	bool keep_data = false;
	u32 flags = 0;

	while (!serializer.isObjectEnd())
	{
		serializer.deserializeLabel(label, sizeof(label));
		if (equalStrings(label, "source"))
		{
			serializer.deserialize(path, MAX_PATH_LENGTH, "");
			if (path[0] != '\0')
			{
				char texture_path[MAX_PATH_LENGTH];
				if (path[0] != '/' && path[0] != '\\')
				{
					copyString(texture_path, material_dir);
					catString(texture_path, path);
				}
				else
				{
					copyString(texture_path, path);
				}
				auto* mng = m_resource_manager.getOwner().get(TEXTURE_TYPE);
				m_textures[m_texture_count] = static_cast<Texture*>(mng->load(Path(texture_path)));
				addDependency(*m_textures[m_texture_count]);
			}
		}
		else if (equalStrings(label, "min_filter"))
		{
			serializer.deserialize(label, sizeof(label), "");
			if (equalStrings(label, "point"))
			{
				flags |= BGFX_TEXTURE_MIN_POINT;
			}
			else if (equalStrings(label, "anisotropic"))
			{
				flags |= BGFX_TEXTURE_MIN_ANISOTROPIC;
			}
			else
			{
				g_log_error.log("Renderer") << "Unknown texture filter \"" << label << "\" in material " << getPath();
			}
		}
		else if (equalStrings(label, "mag_filter"))
		{
			serializer.deserialize(label, sizeof(label), "");
			if (equalStrings(label, "point"))
			{
				flags |= BGFX_TEXTURE_MAG_POINT;
			}
			else if (equalStrings(label, "anisotropic"))
			{
				flags |= BGFX_TEXTURE_MAG_ANISOTROPIC;
			}
			else
			{
				g_log_error.log("Renderer") << "Unknown texture filter \"" << label << "\" in material " << getPath();
			}
		}
		else if (equalStrings(label, "u_clamp"))
		{
			bool b;
			serializer.deserialize(b, false);
			if (b) flags |= BGFX_TEXTURE_U_CLAMP;
		}
		else if (equalStrings(label, "v_clamp"))
		{
			bool b;
			serializer.deserialize(b, false);
			if (b) flags |= BGFX_TEXTURE_V_CLAMP;
		}
		else if (equalStrings(label, "w_clamp"))
		{
			bool b;
			serializer.deserialize(b, false);
			if (b) flags |= BGFX_TEXTURE_W_CLAMP;
		}
		else if (equalStrings(label, "keep_data"))
		{
			serializer.deserialize(keep_data, false);
		}
		else if (equalStrings(label, "srgb"))
		{
			bool is_srgb;
			serializer.deserialize(is_srgb, false);
			if(is_srgb) flags |= BGFX_TEXTURE_SRGB;
		}
		else
		{
			g_log_warning.log("Renderer") << "Unknown data \"" << label << "\" in material " << getPath();
			return false;
		}
	}
	if (m_textures[m_texture_count])
	{
		m_textures[m_texture_count]->setFlags(flags);

		if (keep_data)
		{
			m_textures[m_texture_count]->addDataReference();
		}
	}
	serializer.deserializeObjectEnd();
	++m_texture_count;
	return true;
}


void Material::setAlphaRef(float value)
{
	m_alpha_ref = value;
	u8 val = u8(value * 255.0f);
	m_render_states &= ~BGFX_STATE_ALPHA_REF_MASK;
	m_render_states |= BGFX_STATE_ALPHA_REF(val);
}


void Material::enableBackfaceCulling(bool enable)
{
	if (enable)
	{
		m_render_states |= BGFX_STATE_CULL_CW;
	}
	else
	{
		m_render_states &= ~BGFX_STATE_CULL_MASK;
	}
}


bool Material::isBackfaceCulling() const
{
	return (m_render_states & BGFX_STATE_CULL_MASK) != 0;
}


bool Material::load(FS::IFile& file)
{
	PROFILE_FUNCTION();

	auto& renderer = static_cast<MaterialManager&>(m_resource_manager).getRenderer();

	m_render_states = BGFX_STATE_CULL_CW;
	setAlphaRef(DEFAULT_ALPHA_REF_VALUE);
	m_uniforms.clear();
	JsonSerializer serializer(file, JsonSerializer::READ, getPath(), m_allocator);
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
		else if (equalStrings(label, "shader"))
		{
			Path path;
			serializer.deserialize(path, Path(""));
			auto* manager = m_resource_manager.getOwner().get(SHADER_TYPE);
			setShader(static_cast<Shader*>(manager->load(Path(path))));
		}
		else
		{
			g_log_error.log("Renderer") << "Unknown parameter " << label << " in material "
										  << getPath();
		}
	}
	serializer.deserializeObjectEnd();

	if (!m_shader)
	{
		g_log_error.log("Renderer") << "Material " << getPath() << " without a shader";
		return false;
	}

	m_size = file.size();
	return true;
}


} // ~namespace Lumix
