#pragma once


#include "core/array.h"
#include "core/resource.h"
#include "core/vec3.h"
#include <bgfx/bgfx.h>


namespace Lumix
{

namespace FS
{
	class FileSystem;
	class IFile;
}

class JsonSerializer;
class PipelineInstance;
class ResourceManager;
class Shader;
class ShaderInstance;
class Texture;


class LUMIX_RENDERER_API Material : public Resource
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
		Uniform() {}

		enum Type
		{
			INT,
			FLOAT,
			MATRIX,
			TIME
		};

		static const int MAX_NAME_LENGTH = 32;
		
		char m_name[MAX_NAME_LENGTH + 1];
		uint32_t m_name_hash;
		Type m_type;
		bgfx::UniformHandle m_handle;
		union
		{
			int32_t m_int;
			float m_float;
			float m_matrix[16];
		};
	};

public:
	Material(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	~Material();

	bool isZTest() const { return (m_render_states & BGFX_STATE_DEPTH_TEST_MASK) != 0; }
	void enableZTest(bool enable) { setRenderState(enable, BGFX_STATE_DEPTH_TEST_LEQUAL, BGFX_STATE_DEPTH_TEST_MASK); }
	bool isBackfaceCulling() const { return (m_render_states & BGFX_STATE_CULL_MASK) != 0; }
	void enableBackfaceCulling(bool enable) { setRenderState(enable, BGFX_STATE_CULL_CW, BGFX_STATE_CULL_MASK); }
	bool isAlphaCutout() const;
	bool hasAlphaCutoutDefine() const;
	void enableAlphaCutout(bool enable);
	bool isShadowReceiver() const;
	bool hasShadowReceivingDefine() const;
	void enableShadowReceiving(bool enable);
	float getShininess() const { return m_shininess; }
	void setShininess(float value) { m_shininess = value; }
	Vec3 getSpecular() const { return m_specular; }
	void setSpecular(const Vec3& specular) { m_specular = specular; }
	uint64_t getRenderStates() const { return m_render_states; }

	void setShader(Shader* shader);
	void setShader(const Path& path);
	Shader* getShader() const { return m_shader; }

	int getTextureCount() const { return m_texture_count; }
	Texture* getTexture(int i) const { return i < m_texture_count ? m_textures[i] : nullptr; }
	const char* getTextureUniform(int i);
	Texture* getTextureByUniform(const char* uniform) const;
	void setTexture(int i, Texture* texture);
	void setTexturePath(int i, const Path& path);
	bool save(JsonSerializer& serializer);
	int getUniformCount() const { return m_uniforms.size(); }
	Uniform& getUniform(int index) { return m_uniforms[index]; }
	const Uniform& getUniform(int index) const { return m_uniforms[index]; }
	ShaderInstance& getShaderInstance() { ASSERT(m_shader_instance); return *m_shader_instance; }
	const ShaderInstance& getShaderInstance() const { ASSERT(m_shader_instance); return *m_shader_instance; }
	void setUserDefine(int define_idx);
	void unsetUserDefine(int define_idx);

private:
	virtual void onBeforeReady() override;
	virtual void unload(void) override;
	virtual bool load(FS::IFile& file) override;

	bool deserializeTexture(JsonSerializer& serializer, const char* material_dir);
	void deserializeUniforms(JsonSerializer& serializer);
	void setRenderState(bool value, uint64_t state, uint64_t mask);

private:
	static const int MAX_TEXTURE_COUNT = 16;

	Shader*	m_shader;
	ShaderInstance* m_shader_instance;
	Texture* m_textures[MAX_TEXTURE_COUNT];
	int m_texture_count;
	Array<Uniform> m_uniforms;
	IAllocator& m_allocator;
	bgfx::ProgramHandle m_program_id;
	uint64_t m_render_states;
	Vec3 m_specular;
	float m_shininess;
	uint32_t m_shader_mask;
	static int s_alpha_cutout_define_idx;
	static int s_shadow_receiver_define_idx;
};

} // ~namespace Lumix
