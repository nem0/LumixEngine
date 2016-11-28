#pragma once


#include "engine/array.h"
#include "engine/resource.h"
#include "engine/vec.h"
#include <bgfx/bgfx.h>


namespace Lumix
{

namespace FS
{
	class IFile;
}

class JsonSerializer;
class ResourceManager;
class Shader;
struct ShaderInstance;
class Texture;


class LUMIX_RENDERER_API Material LUMIX_FINAL : public Resource
{
	friend class MaterialManager;
public:
	enum class DepthFunc
	{
		LEQUAL,
		LESS
	};

	struct Uniform
	{
		u32 name_hash;
		union
		{
			i32 int_value;
			float float_value;
			float vec3[3];
			float vec2[2];
			float matrix[16];
		};
	};

public:
	Material(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
	~Material();

	float getMetallic() const { return m_metallic; }
	void setMetallic(float value) { m_metallic = value; createCommandBuffer(); }
	float getRoughness() const { return m_roughness; }
	void setRoughness(float value) { m_roughness = value; createCommandBuffer(); }
	Vec3 getColor() const { return m_color; }
	void setColor(const Vec3& specular) { m_color = specular;  createCommandBuffer(); }
	float getAlphaRef() const { return m_alpha_ref; }
	void setAlphaRef(float value);
	u64 getRenderStates() const { return m_render_states; }
	void enableBackfaceCulling(bool enable);
	bool isBackfaceCulling() const;

	void setShader(Shader* shader);
	void setShader(const Path& path);
	Shader* getShader() const { return m_shader; }

	int getTextureCount() const { return m_texture_count; }
	Texture* getTexture(int i) const { return i < m_texture_count ? m_textures[i] : nullptr; }
	const char* getTextureUniform(int i);
	Texture* getTextureByUniform(const char* uniform) const;
	bool isTextureDefine(u8 define_idx) const;
	void setTexture(int i, Texture* texture);
	void setTexturePath(int i, const Path& path);
	bool save(JsonSerializer& serializer);
	int getUniformCount() const { return m_uniforms.size(); }
	Uniform& getUniform(int index) { return m_uniforms[index]; }
	const Uniform& getUniform(int index) const { return m_uniforms[index]; }
	ShaderInstance& getShaderInstance() { ASSERT(m_shader_instance); return *m_shader_instance; }
	const ShaderInstance& getShaderInstance() const { ASSERT(m_shader_instance); return *m_shader_instance; }
	const u8* getCommandBuffer() const { return m_command_buffer; }
	void createCommandBuffer();
	int getRenderLayer() const { return m_render_layer; }
	void setRenderLayer(int layer);
	u64 getRenderLayerMask() const { return m_render_layer_mask; }
	int getLayersCount() const { return m_layers_count; }
	void setLayersCount(int layers);

	void setDefine(u8 define_idx, bool enabled);
	bool hasDefine(u8 define_idx) const;
	bool isDefined(u8 define_idx) const;

	void setCustomFlag(u32 flag) { m_custom_flags |= flag; }
	void unsetCustomFlag(u32 flag) { m_custom_flags &= ~flag; }
	bool isCustomFlag(u32 flag) const { return (m_custom_flags & flag) == flag; }

	static u32 getCustomFlag(const char* flag_name);
	static const char* getCustomFlagName(int index);
	static int getCustomFlagCount();

private:
	void onBeforeReady() override;
	void unload(void) override;
	bool load(FS::IFile& file) override;

	bool deserializeTexture(JsonSerializer& serializer, const char* material_dir);
	void deserializeUniforms(JsonSerializer& serializer);
	void deserializeDefines(JsonSerializer& serializer);
	void deserializeCustomFlags(JsonSerializer& serializer);

private:
	static const int MAX_TEXTURE_COUNT = 16;

	Shader* m_shader;
	ShaderInstance* m_shader_instance;
	Texture* m_textures[MAX_TEXTURE_COUNT];
	int m_texture_count;
	Array<Uniform> m_uniforms;
	IAllocator& m_allocator;
	u64 m_render_states;
	Vec3 m_color;
	float m_alpha_ref;
	float m_metallic;
	float m_roughness;
	u32 m_define_mask;
	u8* m_command_buffer;
	u32 m_custom_flags;
	int m_render_layer;
	u64 m_render_layer_mask;
	int m_layers_count;
};

} // ~namespace Lumix
