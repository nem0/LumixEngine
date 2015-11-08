#include "renderer/material.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/timer.h"
#include "renderer/frame_buffer.h"
#include "renderer/material_manager.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"


namespace Lumix
{


static const uint32_t SHADOWMAP_HASH = crc32("shadowmap");
int Material::s_alpha_cutout_define_idx = -1;
int Material::s_shadow_receiver_define_idx = -1;


Material::Material(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_shader(nullptr)
	, m_uniforms(allocator)
	, m_allocator(allocator)
	, m_texture_count(0)
	, m_render_states(0)
	, m_specular(1, 1, 1)
	, m_shininess(4)
	, m_shader_instance(nullptr)
	, m_shader_mask(0)
{
	auto* manager = resource_manager.get(ResourceManager::MATERIAL);
	auto* mat_manager = static_cast<MaterialManager*>(manager);

	s_alpha_cutout_define_idx = mat_manager->getRenderer().getShaderDefineIdx("ALPHA_CUTOUT");
	s_shadow_receiver_define_idx = mat_manager->getRenderer().getShaderDefineIdx("SHADOW_RECEIVER");

	enableZTest(true);
	enableBackfaceCulling(true);
	enableShadowReceiving(true);
	for (int i = 0; i < MAX_TEXTURE_COUNT; ++i)
	{
		m_textures[i] = nullptr;
	}
}


Material::~Material()
{
	ASSERT(isEmpty());
}


void Material::setUserDefine(int define_idx)
{
	if (!isReady()) return;
	if (!m_shader) return;

	uint32_t old_mask = m_shader_mask;
	m_shader_mask |= m_shader->getDefineMask(define_idx);

	if (old_mask != m_shader_mask)
	{
		m_shader_instance = &m_shader->getInstance(m_shader_mask);
	}
}


void Material::unsetUserDefine(int define_idx)
{
	if (!isReady()) return;
	if (!m_shader) return;

	uint32_t old_mask = m_shader_mask;
	m_shader_mask &= ~m_shader->getDefineMask(define_idx);

	if (old_mask != m_shader_mask)
	{
		m_shader_instance = &m_shader->getInstance(m_shader_mask);
	}
}


bool Material::isAlphaCutout() const
{
	if (!isReady()) return false;
	if (!m_shader)	return false;

	return (m_shader_mask & m_shader->getDefineMask(s_alpha_cutout_define_idx)) != 0;
}


bool Material::hasAlphaCutoutDefine() const
{
	return m_shader->getDefineMask(s_alpha_cutout_define_idx) != 0;
}


void Material::enableAlphaCutout(bool enable)
{
	if (!isReady()) return;
	if (!m_shader)	return;

	uint32_t mask = m_shader->getDefineMask(s_alpha_cutout_define_idx);
	if (enable)
	{
		m_shader_mask |= mask;
	}
	else
	{
		m_shader_mask &= ~mask;
	}

	m_shader_instance = &m_shader->getInstance(m_shader_mask);
}


bool Material::isShadowReceiver() const
{
	if (!isReady()) return false;
	if (!m_shader)	return false;

	return (m_shader_mask & m_shader->getDefineMask(s_shadow_receiver_define_idx)) != 0;
}


bool Material::hasShadowReceivingDefine() const
{
	return m_shader->getDefineMask(s_shadow_receiver_define_idx) != 0;
}


void Material::enableShadowReceiving(bool enable)
{
	if (!isReady()) return;
	if (!m_shader)	return;

	uint32_t mask = m_shader->getDefineMask(s_shadow_receiver_define_idx);
	if (enable)
	{
		m_shader_mask |= mask;
	}
	else
	{
		m_shader_mask &= ~mask;
	}

	m_shader_instance = &m_shader->getInstance(m_shader_mask);
}


void Material::unload(void)
{
	setShader(nullptr);

	ResourceManagerBase* texture_manager = m_resource_manager.get(ResourceManager::TEXTURE);
	for (int i = 0; i < m_texture_count; i++)
	{
		if (m_textures[i])
		{
			removeDependency(*m_textures[i]);
			texture_manager->unload(*m_textures[i]);
		}
	}
	m_texture_count = 0;
}


bool Material::save(JsonSerializer& serializer)
{
	serializer.beginObject();
	serializer.serialize("shader", m_shader ? m_shader->getPath().c_str() : "");
	for (int i = 0; i < m_texture_count; ++i)
	{
		char path[MAX_PATH_LENGTH];
		int flags = 0;
		int atlas_size = -1;
		if (m_textures[i])
		{
			flags = m_textures[i]->getFlags();
			path[0] = '/';
			Lumix::copyString(path + 1, MAX_PATH_LENGTH - 1, m_textures[i]->getPath().c_str());
			atlas_size = m_textures[i]->getAtlasSize();
		}
		else
		{
			path[0] = '\0';
		}
		serializer.beginObject("texture");
		serializer.serialize("source", path);
		if (atlas_size > 0) serializer.serialize("atlas_size", atlas_size);
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
	serializer.beginArray("uniforms");
	for (int i = 0; i < m_uniforms.size(); ++i)
	{
		serializer.beginObject();
		serializer.serialize("name", m_uniforms[i].m_name);
		switch (m_uniforms[i].m_type)
		{
			case Uniform::FLOAT:
				serializer.serialize("float_value", m_uniforms[i].m_float);
				break;
			case Uniform::TIME:
				serializer.serialize("time", m_uniforms[i].m_float);
				break;
			case Uniform::INT:
				serializer.serialize("int_value", m_uniforms[i].m_int);
				break;
			case Uniform::MATRIX:
				serializer.beginArray("matrix_value");
				for (int j = 0; j < 16; ++j)
				{
					serializer.serializeArrayItem(m_uniforms[i].m_matrix[j]);
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
	serializer.serialize("backface_culling", isBackfaceCulling());
	serializer.serialize("alpha_cutout", isAlphaCutout());
	serializer.serialize("shadow_receiver", isShadowReceiver());
	serializer.serialize("shininess", m_shininess);
	serializer.beginArray("specular");
	serializer.serializeArrayItem(m_specular.x);
	serializer.serializeArrayItem(m_specular.y);
	serializer.serializeArrayItem(m_specular.z);
	serializer.endArray();
	serializer.serialize("z_test", isZTest());
	serializer.endObject();
	return true;
}

void Material::deserializeUniforms(JsonSerializer& serializer)
{
	serializer.deserializeArrayBegin();
	m_uniforms.clear();
	while (!serializer.isArrayEnd())
	{
		Uniform& uniform = m_uniforms.pushEmpty();
		serializer.nextArrayItem();
		serializer.deserializeObjectBegin();
		char label[256];
		auto uniform_type = bgfx::UniformType::End;
		while (!serializer.isObjectEnd())
		{
			serializer.deserializeLabel(label, 255);
			if (strcmp(label, "name") == 0)
			{
				serializer.deserialize(uniform.m_name, Uniform::MAX_NAME_LENGTH, "");
				uniform.m_name_hash = crc32(uniform.m_name);
			}
			else if (strcmp(label, "int_value") == 0)
			{
				uniform_type = bgfx::UniformType::Int1;
				uniform.m_type = Uniform::INT;
				serializer.deserialize(uniform.m_int, 0);
			}
			else if (strcmp(label, "float_value") == 0)
			{
				uniform.m_type = Uniform::FLOAT;
				serializer.deserialize(uniform.m_float, 0);
			}
			else if (strcmp(label, "matrix_value") == 0)
			{
				uniform_type = bgfx::UniformType::Mat4;
				uniform.m_type = Uniform::MATRIX;
				serializer.deserializeArrayBegin();
				for (int i = 0; i < 16; ++i)
				{
					serializer.deserializeArrayItem(uniform.m_matrix[i], 0);
					ASSERT(i == 15 || !serializer.isArrayEnd());
				}
				serializer.deserializeArrayEnd();
			}
			else if (strcmp(label, "time") == 0)
			{
				uniform.m_type = Uniform::TIME;
				serializer.deserialize(uniform.m_float, 0);
			}
			else
			{
				g_log_warning.log("material") << "Unknown label \"" << label << "\"";
			}
		}
		uniform.m_handle = bgfx::createUniform(uniform.m_name, uniform_type);

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
		Texture* texture =
			static_cast<Texture*>(m_resource_manager.get(ResourceManager::TEXTURE)->load(path));
		setTexture(i, texture);
	}
}


void Material::setTexture(int i, Texture* texture)
{ 
	Texture* old_texture = i < m_texture_count ? m_textures[i] : nullptr;

	if (texture) addDependency(*texture);
	m_textures[i] = texture;
	if (i >= m_texture_count) m_texture_count = i + 1;

	if (old_texture)
	{
		if (texture) texture->setAtlasSize(old_texture->getAtlasSize());
		removeDependency(*old_texture);
		m_resource_manager.get(ResourceManager::TEXTURE)->unload(*old_texture);
	}
	if (isReady() && m_shader)
	{
		int define_idx = m_shader->getTextureSlot(i).m_define_idx;
		if (define_idx >= 0 && m_textures[i])
		{
			m_shader_mask |= m_shader->getDefineMask(define_idx);
		}
		else
		{
			m_shader_mask &= ~m_shader->getDefineMask(define_idx);
		}

		m_shader_instance = &m_shader->getInstance(m_shader_mask);
	}
}


void Material::setShader(const Path& path)
{
	Shader* shader = static_cast<Shader*>(m_resource_manager.get(ResourceManager::SHADER)->load(path));
	setShader(shader);
}


void Material::onBeforeReady()
{
	if (!m_shader) return;

	for (int i = 0; i < m_shader->getTextureSlotCount(); ++i)
	{
		if (m_shader->getTextureSlot(i).m_define_idx >= 0 && m_textures[i])
		{
			m_shader_mask |= m_shader->getDefineMask(
				m_shader->getTextureSlot(i).m_define_idx);
		}
	}
	m_shader_instance = &m_shader->getInstance(m_shader_mask);
}


void Material::setShader(Shader* shader)
{
	bool is_alpha = isAlphaCutout();
	bool is_receiver = isShadowReceiver();
	if (m_shader)
	{
		Shader* shader = m_shader;
		m_shader = nullptr;
		removeDependency(*shader);
		m_resource_manager.get(ResourceManager::SHADER)->unload(*shader);
	}
	m_shader = shader;
	if (m_shader)
	{
		addDependency(*m_shader);

		if (m_shader->isReady())
		{
			m_shader_mask = 0;
			enableShadowReceiving(is_receiver);
			enableAlphaCutout(is_alpha);

			for (int i = 0; i < m_shader->getTextureSlotCount(); ++i)
			{
				if (m_shader->getTextureSlot(i).m_define_idx >= 0 && m_textures[i])
				{
					m_shader_mask |= m_shader->getDefineMask(
						m_shader->getTextureSlot(i).m_define_idx);
				}
			}
			m_shader_instance = &m_shader->getInstance(m_shader_mask);
		}
	}
}


const char* Material::getTextureUniform(int i)
{
	if (i < m_shader->getTextureSlotCount())
	{
		return m_shader->getTextureSlot(i).m_uniform;
	}
	return "";
}


Texture* Material::getTextureByUniform(const char* uniform) const
{
	if (!m_shader)
	{
		return nullptr;
	}

	for (int i = 0, c = m_shader->getTextureSlotCount(); i < c; ++i)
	{
		if (strcmp(m_shader->getTextureSlot(i).m_uniform, uniform) == 0)
		{
			return m_textures[i];
		}
	}
	return nullptr;
}

bool Material::deserializeTexture(JsonSerializer& serializer, const char* material_dir)
{
	char path[MAX_PATH_LENGTH];
	serializer.deserializeObjectBegin();
	char label[256];
	bool keep_data = false;
	uint32_t flags = 0;
	int atlas_size = -1;

	while (!serializer.isObjectEnd())
	{
		serializer.deserializeLabel(label, sizeof(label));
		if (strcmp(label, "source") == 0)
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
				m_textures[m_texture_count] = static_cast<Texture*>(
					m_resource_manager.get(ResourceManager::TEXTURE)->load(Path(texture_path)));
				addDependency(*m_textures[m_texture_count]);
			}
		}
		else if (strcmp(label, "atlas_size") == 0)
		{
			serializer.deserialize(atlas_size, -1);
		}
		else if (strcmp(label, "min_filter") == 0)
		{
			serializer.deserialize(label, sizeof(label), "");
			if (strcmp(label, "point") == 0)
			{
				flags |= BGFX_TEXTURE_MIN_POINT;
			}
			else if (strcmp(label, "anisotropic") == 0)
			{
				flags |= BGFX_TEXTURE_MIN_ANISOTROPIC;
			}
			else
			{
				g_log_error.log("Renderer") << "Unknown texture filter \"" << label
											<< "\" in material " << getPath().c_str();
			}
		}
		else if (strcmp(label, "mag_filter") == 0)
		{
			serializer.deserialize(label, sizeof(label), "");
			if (strcmp(label, "point") == 0)
			{
				flags |= BGFX_TEXTURE_MAG_POINT;
			}
			else if (strcmp(label, "anisotropic") == 0)
			{
				flags |= BGFX_TEXTURE_MAG_ANISOTROPIC;
			}
			else
			{
				g_log_error.log("Renderer") << "Unknown texture filter \"" << label
											<< "\" in material " << getPath().c_str();
			}
		}
		else if (strcmp(label, "u_clamp") == 0)
		{
			bool b;
			serializer.deserialize(b, false);
			if (b)
			{
				flags |= BGFX_TEXTURE_U_CLAMP;
			}
		}
		else if (strcmp(label, "v_clamp") == 0)
		{
			bool b;
			serializer.deserialize(b, false);
			if (b)
			{
				flags |= BGFX_TEXTURE_V_CLAMP;
			}
		}
		else if (strcmp(label, "w_clamp") == 0)
		{
			bool b;
			serializer.deserialize(b, false);
			if (b)
			{
				flags |= BGFX_TEXTURE_W_CLAMP;
			}
		}
		else if (strcmp(label, "keep_data") == 0)
		{
			serializer.deserialize(keep_data, false);
		}
		else
		{
			g_log_warning.log("Renderer") << "Unknown data \"" << label << "\" in material "
										  << getPath().c_str();
			return false;
		}
	}
	if (m_textures[m_texture_count])
	{
		m_textures[m_texture_count]->setAtlasSize(atlas_size);
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


void Material::setRenderState(bool value, uint64_t state, uint64_t mask)
{
	if (value)
	{
		m_render_states |= state;
	}
	else
	{
		m_render_states &= ~mask;
	}
}


bool Material::load(FS::IFile& file)
{
	PROFILE_FUNCTION();

	m_render_states = BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_CULL_CW;
	m_uniforms.clear();
	JsonSerializer serializer(file, JsonSerializer::READ, getPath().c_str(), m_allocator);
	serializer.deserializeObjectBegin();
	char path[MAX_PATH_LENGTH];
	char label[256];
	char material_dir[MAX_PATH_LENGTH];
	PathUtils::getDir(material_dir, MAX_PATH_LENGTH, getPath().c_str());
	bool b_value;
	while (!serializer.isObjectEnd())
	{
		serializer.deserializeLabel(label, 255);
		if (strcmp(label, "uniforms") == 0)
		{
			deserializeUniforms(serializer);
		}
		else if (strcmp(label, "texture") == 0)
		{
			if (!deserializeTexture(serializer, material_dir))
			{
				return false;
			}
		}
		else if (strcmp(label, "alpha_cutout") == 0)
		{
			bool b;
			serializer.deserialize(b, false);
			enableAlphaCutout(b);
		}
		else if (strcmp(label, "alpha_blending") == 0)
		{
			if (serializer.isNextBoolean())
			{
				bool is_alpha_blending;
				serializer.deserialize(is_alpha_blending, false);
				if (is_alpha_blending)
				{
					m_render_states |= BGFX_STATE_BLEND_ADD;
				}
				else
				{
					m_render_states &= ~BGFX_STATE_BLEND_MASK;
				}
			}
			else
			{
				serializer.deserialize(label, 255, "alpha");
				if (strcmp(label, "alpha") == 0)
				{
					m_render_states |= BGFX_STATE_BLEND_ALPHA;
				}
				else if (strcmp(label, "add") == 0)
				{
					m_render_states |= BGFX_STATE_BLEND_ADD;
				}
				else if (strcmp(label, "disabled") == 0)
				{
					m_render_states &= ~BGFX_STATE_BLEND_MASK;
				}
			}
		}
		else if (strcmp(label, "specular") == 0)
		{
			serializer.deserializeArrayBegin();
			serializer.deserializeArrayItem(m_specular.x, 1.0f);
			serializer.deserializeArrayItem(m_specular.y, 1.0f);
			serializer.deserializeArrayItem(m_specular.z, 1.0f);
			serializer.deserializeArrayEnd();
		}
		else if (strcmp(label, "shininess") == 0)
		{
			serializer.deserialize(m_shininess, 4.0f);
		}
		else if (strcmp(label, "shadow_receiver") == 0)
		{
			bool b;
			serializer.deserialize(b, true);
			enableShadowReceiving(b);
		}
		else if (strcmp(label, "shader") == 0)
		{
			serializer.deserialize(path, MAX_PATH_LENGTH, "");
			setShader(static_cast<Shader*>(
				m_resource_manager.get(ResourceManager::SHADER)->load(Path(path))));
		}
		else if (strcmp(label, "z_test") == 0)
		{
			serializer.deserialize(b_value, true);
			enableZTest(b_value);
		}
		else if (strcmp(label, "backface_culling") == 0)
		{
			serializer.deserialize(b_value, true);
			enableBackfaceCulling(b_value);
		}
		else
		{
			g_log_warning.log("renderer") << "Unknown parameter " << label << " in material "
										  << getPath().c_str();
		}
	}
	serializer.deserializeObjectEnd();

	if (!m_shader)
	{
		g_log_error.log("renderer") << "Material " << getPath().c_str() << " without a shader";
		return false;
	}

	m_size = file.size();
	return true;
}


} // ~namespace Lumix
