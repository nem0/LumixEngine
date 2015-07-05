#pragma once


#include "core/array.h"
#include "core/resource.h"
#include <bgfx.h>


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


class LUMIX_ENGINE_API Material : public Resource
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
	bool isZTest() const { return (m_render_states & BGFX_STATE_DEPTH_TEST_MASK) != 0; }
	void enableZTest(bool enable) { setRenderState(enable, BGFX_STATE_DEPTH_TEST_LEQUAL, BGFX_STATE_DEPTH_TEST_MASK); }
	bool isBackfaceCulling() const { return (m_render_states & BGFX_STATE_CULL_MASK) != 0; }
	void enableBackfaceCulling(bool enable) { setRenderState(enable, BGFX_STATE_CULL_CW, BGFX_STATE_CULL_MASK); }
	bool isAlphaCutout() const { return m_is_alpha_cutout; }
	void enableAlphaCutout(bool enable) { m_is_alpha_cutout = enable; updateShaderInstance(); }
	bool isShadowReceiver() const { return m_is_shadow_receiver; }
	void enableShadowReceiving(bool enable) { m_is_shadow_receiver = enable; updateShaderInstance(); }
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

	Material(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
		, m_shader(NULL)
		, m_depth_func(DepthFunc::LEQUAL)
		, m_is_alpha_cutout(false)
		, m_is_shadow_receiver(true)
		, m_uniforms(allocator)
		, m_allocator(allocator)
		, m_texture_count(0)
		, m_render_states(0)
		, m_shader_instance(nullptr)
	{ 
		for (int i = 0; i < MAX_TEXTURE_COUNT; ++i)
		{
			m_textures[i] = nullptr;
		}
		updateShaderInstance();
	}

	~Material();

private:
	virtual void onReady(void) override;
	virtual void doUnload(void) override;
	virtual void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) override;

	bool deserializeTexture(JsonSerializer& serializer, const char* material_dir);

private:
	void deserializeUniforms(JsonSerializer& serializer);
	void updateShaderInstance();
	void setRenderState(bool value, uint64_t state, uint64_t mask);

private:
	static const int MAX_TEXTURE_COUNT = 16;

	Shader*	m_shader;
	ShaderInstance* m_shader_instance;
	Texture* m_textures[MAX_TEXTURE_COUNT];
	int m_texture_count;
	Array<Uniform> m_uniforms;
	bool m_is_alpha_cutout;
	bool m_is_shadow_receiver;
	DepthFunc m_depth_func;
	IAllocator& m_allocator;
	bgfx::ProgramHandle m_program_id;
	uint64_t m_render_states;
};

} // ~namespace Lumix
