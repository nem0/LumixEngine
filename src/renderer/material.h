#pragma once


#include "engine/array.h"
#include "engine/hash.h"
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

struct LUMIX_RENDERER_API Material final : Resource {
	friend struct MaterialManager;
	static const int MAX_TEXTURE_COUNT = 16;

	static constexpr u32 MAX_UNIFORMS_FLOATS = 64; 
	static constexpr u32 MAX_UNIFORMS_BYTES = MAX_UNIFORMS_FLOATS * sizeof(float); 

	struct Uniform
	{
		RuntimeHash name_hash;
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

	Material(const Path& path, ResourceManager& resource_manager, Renderer& renderer, IAllocator& allocator);
	~Material();

	ResourceType getType() const override { return TYPE; }
	Renderer& getRenderer() { return m_renderer; }
	void enableBackfaceCulling(bool enable);
	bool isBackfaceCulling() const;
	bool isAlphaCutout() const;
	void setAlphaCutout(bool enable);

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
	Uniform* findUniform(RuntimeHash name_hash);

	void setDefine(u8 define_idx, bool enabled);
	bool isDefined(u8 define_idx) const;
	u32 getDefineMask() const { return m_define_mask; }

	void setCustomFlag(u32 flag) { m_custom_flags |= flag; }
	void unsetCustomFlag(u32 flag) { m_custom_flags &= ~flag; }
	bool isCustomFlag(u32 flag) const { return (m_custom_flags & flag) == flag; }

	bool wireframe() const;
	void setWireframe(bool enable);

	u8 getLayer() const { return m_layer; }
	void setLayer(u8 layer);
	u32 getSortKey() const { return m_sort_key; }

	static u32 getCustomFlag(const char* flag_name);
	static const char* getCustomFlagName(int index);
	static int getCustomFlagCount();
	void updateRenderData(bool on_before_ready);
	Array<Uniform>& getUniforms() { return m_uniforms; }

	gpu::BindGroupHandle m_bind_group = gpu::INVALID_BIND_GROUP;
	gpu::StateFlags m_render_states;

private:
	void onBeforeReady() override;
	void unload() override;
	bool load(u64 size, const u8* mem) override;

	static int uniform(lua_State* L);
	static int int_uniform(lua_State* L);

	Renderer& m_renderer;
	Shader* m_shader;
	Texture* m_textures[MAX_TEXTURE_COUNT];
	u8 m_layer;
	u32 m_sort_key;
	u32 m_define_mask;
	u32 m_material_constants = 0;
	u32 m_texture_count;

	Array<Uniform> m_uniforms;
	u32 m_custom_flags;
};

} // namespace Lumix
