#pragma once


#include "engine/array.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/math.h"
#include "gpu/gpu.h"


struct lua_State;


namespace Lumix
{

struct Renderer;
struct ResourceManagerHub;
struct Shader;
struct Texture;

struct  MaterialConsts {
	Vec4 color;
	float roughness;
	float metallic;
	float emission;
	float padding;
	float custom[56];
};

struct MaterialManager : ResourceManager {
public:
	MaterialManager(Renderer& renderer, IAllocator& allocator);
	~MaterialManager() override;

	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;
	lua_State* getState(struct Material& material) const;

private:
	Renderer& m_renderer; 
	lua_State* m_state;
};

struct LUMIX_RENDERER_API Material final : Resource
{
friend struct MaterialManager;
public:
	static const int MAX_TEXTURE_COUNT = 16;

	struct RenderData {
		gpu::TextureHandle textures[MAX_TEXTURE_COUNT];
		int textures_count;
		u64 render_states;
		u32 material_constants;
		u32 define_mask;
	};

	struct Uniform
	{
		u32 name_hash;
		union
		{
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
	void setMetallic(float value) { m_metallic = value; updateRenderData(false); }
	float getRoughness() const { return m_roughness; }
	void setRoughness(float value) { m_roughness = value; updateRenderData(false); }
	float getEmission() const { return m_emission; }
	void setEmission(float value) { m_emission = value; updateRenderData(false); }
	Vec4 getColor() const { return m_color; }
	void setColor(const Vec4& color) { m_color = color; updateRenderData(false); }
	float getAlphaRef() const { return m_alpha_ref; }
	void setAlphaRef(float value) { m_alpha_ref = value; updateRenderData(false); }
	u64 getRenderStates() const { return m_render_states; }
	void enableBackfaceCulling(bool enable);
	bool isBackfaceCulling() const;

	void setShader(Shader* shader);
	void setShader(const Path& path);
	Shader* getShader() const { return m_shader; }

	int getTextureCount() const { return m_texture_count; }
	Texture* getTexture(u32 i) const { return i < m_texture_count ? m_textures[i] : nullptr; }
	Texture* getTextureByName(const char* name) const;
	bool isTextureDefine(u8 define_idx) const;
	void setTexture(u32 i, Texture* texture);
	void setTexturePath(int i, const Path& path);
	bool save(struct IOutputStream& file);
	int getUniformCount() const { return m_uniforms.size(); }
	Uniform& getUniform(int index) { return m_uniforms[index]; }
	const Uniform& getUniform(int index) const { return m_uniforms[index]; }
	Uniform* findUniform(u32 name_hash);

	void setDefine(u8 define_idx, bool enabled);
	bool isDefined(u8 define_idx) const;
	u32 getDefineMask() const { return m_define_mask; }

	void setCustomFlag(u32 flag) { m_custom_flags |= flag; }
	void unsetCustomFlag(u32 flag) { m_custom_flags &= ~flag; }
	bool isCustomFlag(u32 flag) const { return (m_custom_flags & flag) == flag; }

	u8 getLayer() const { return m_layer; }
	void setLayer(u8 layer);
	u32 getSortKey() const { return m_sort_key; }

	static u32 getCustomFlag(const char* flag_name);
	static const char* getCustomFlagName(int index);
	static int getCustomFlagCount();
	void updateRenderData(bool on_before_ready);

private:
	void onBeforeReady() override;
	void unload() override;
	bool load(u64 size, const u8* mem) override;

	static int uniform(lua_State* L);

private:
	Renderer& m_renderer;
	Shader* m_shader;
	float m_metallic;
	float m_roughness;
	float m_emission;
	Vec4 m_color;
	float m_alpha_ref;
	Texture* m_textures[MAX_TEXTURE_COUNT];
	u32 m_texture_count;
	u32 m_define_mask;
	u64 m_render_states;
	RenderData* m_render_data;
	u8 m_layer;
	u32 m_sort_key;

	Array<Uniform> m_uniforms;
	u32 m_custom_flags;
};

} // namespace Lumix
