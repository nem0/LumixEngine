#pragma once


#include "engine/array.h"
#include "engine/resource.h"
#include "engine/vec.h"
#include "ffr/ffr.h"


struct lua_State;


namespace Lumix
{

namespace FS
{
	struct IFile;
}

class Renderer;
class ResourceManagerHub;
class Shader;
struct ShaderRenderData;
class Texture;


class LUMIX_RENDERER_API Material final : public Resource
{
	friend class MaterialManager;
public:
	static const int MAX_TEXTURE_COUNT = 16;

	struct RenderData {
		ffr::TextureHandle textures[MAX_TEXTURE_COUNT];
		int textures_count;
		u64 render_states;
		Vec4 color;
		float roughness;
		float metallic;
		float emission;
		ShaderRenderData* shader;
	};

	struct Uniform
	{
		u32 name_hash;
		union
		{
			i32 int_value;
			float float_value;
			float vec4[4];
			float vec3[3];
			float vec2[2];
			float matrix[16];
		};
	};

	static const ResourceType TYPE;

public:
	Material(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);
	~Material();

	ResourceType getType() const override { return TYPE; }

	Renderer& getRenderer() { return m_renderer; }
	RenderData* getRenderData() const { return m_render_data; }

	float getMetallic() const { return m_metallic; }
	void setMetallic(float value) { m_metallic = value; }
	float getRoughness() const { return m_roughness; }
	void setRoughness(float value) { m_roughness = value; }
	float getEmission() const { return m_emission; }
	void setEmission(float value) { m_emission = value; }
	Vec4 getColor() const { return m_color; }
	void setColor(const Vec4& color) { m_color = color; }
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
	ffr::UniformHandle getTextureUniform(int i) const;
	Texture* getTextureByUniform(const char* uniform) const;
	bool isTextureDefine(u8 define_idx) const;
	void setTexture(int i, Texture* texture);
	void setTexturePath(int i, const Path& path);
	bool save(FS::IFile& file);
	int getUniformCount() const { return m_uniforms.size(); }
	Uniform& getUniform(int index) { return m_uniforms[index]; }
	const Uniform& getUniform(int index) const { return m_uniforms[index]; }

	void setDefine(u8 define_idx, bool enabled);
	bool isDefined(u8 define_idx) const;
	u32 getDefineMask() const { return m_define_mask; }

	void setCustomFlag(u32 flag) { m_custom_flags |= flag; }
	void unsetCustomFlag(u32 flag) { m_custom_flags &= ~flag; }
	bool isCustomFlag(u32 flag) const { return (m_custom_flags & flag) == flag; }

	u8 getLayer() const { return m_layer; }
	void setLayer(u8 layer) { m_layer = layer; }

	static u32 getCustomFlag(const char* flag_name);
	static const char* getCustomFlagName(int index);
	static int getCustomFlagCount();

private:
	static int LUA_layer(lua_State* L);

	void onBeforeReady() override;
	void unload() override;
	bool load(FS::IFile& file) override;

	void deserializeUniforms(lua_State* L);

private:

	IAllocator& m_allocator;
	Renderer& m_renderer;
	Shader* m_shader;
	float m_metallic;
	float m_roughness;
	float m_emission;
	Vec4 m_color;
	float m_alpha_ref;
	Texture* m_textures[MAX_TEXTURE_COUNT];
	int m_texture_count;
	u32 m_define_mask;
	u64 m_render_states;
	RenderData* m_render_data;
	u8 m_layer;
	
	Array<Uniform> m_uniforms;
	u32 m_custom_flags;
};

} // namespace Lumix
