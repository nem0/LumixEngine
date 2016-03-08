#pragma once


#include "core/array.h"
#include "core/resource.h"
#include "core/vec.h"
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
		uint32 name_hash;
		union
		{
			int32 int_value;
			float float_value;
			float vec3[3];
			float matrix[16];
		};
	};

public:
	Material(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
	~Material();

	float getShininess() const { return m_shininess; }
	void setShininess(float value) { m_shininess = value; }
	Vec3 getColor() const { return m_color; }
	void setColor(const Vec3& specular) { m_color = specular; }
	float getAlphaRef() const { return m_alpha_ref; }
	void setAlphaRef(float value);
	uint64 getRenderStates() const { return m_render_states; }

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
	const uint8* getCommandBuffer() const { return m_command_buffer; }
	int getLayerCount() const { return m_layer_count; }
	void setLayerCount(int count) { m_layer_count = count; }
	void createCommandBuffer();

	void setDefine(uint8 define_idx, bool enabled);
	bool hasDefine(uint8 define_idx) const;
	bool isDefined(uint8 define_idx) const;

private:
	void onBeforeReady() override;
	void unload(void) override;
	bool load(FS::IFile& file) override;

	bool deserializeTexture(JsonSerializer& serializer, const char* material_dir);
	void deserializeUniforms(JsonSerializer& serializer);
	void deserializeDefines(JsonSerializer& serializer);

private:
	static const int MAX_TEXTURE_COUNT = 16;

	Shader* m_shader;
	ShaderInstance* m_shader_instance;
	Texture* m_textures[MAX_TEXTURE_COUNT];
	int m_texture_count;
	Array<Uniform> m_uniforms;
	IAllocator& m_allocator;
	bgfx::ProgramHandle m_program_id;
	uint64 m_render_states;
	Vec3 m_color;
	float m_shininess;
	float m_alpha_ref;
	uint32 m_define_mask;
	uint8* m_command_buffer;
	int m_layer_count;
};

} // ~namespace Lumix
