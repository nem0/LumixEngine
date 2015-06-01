#pragma once


#include "core/array.h"
#include "core/resource.h"


namespace Lumix
{
namespace FS
{
	class FileSystem;
	class IFile;
}

class JsonSerializer;
class PipelineInstance;
class Renderer;
class ResourceManager;
class Shader;
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
		union
		{
			int32_t m_int;
			float m_float;
			float m_matrix[16];
		};
	};

public:
	void apply(Renderer& renderer, PipelineInstance& pipeline) const;
	bool isZTest() const { return m_is_z_test; }
	void enableZTest(bool enable) { m_is_z_test = enable; }
	bool isBackfaceCulling() const { return m_is_backface_culling; }
	void enableBackfaceCulling(bool enable) { m_is_backface_culling = enable; }
	bool isAlphaToCoverage() const { return m_is_alpha_to_coverage; }
	void enableAlphaToCoverage(bool enable) { m_is_alpha_to_coverage = enable; }
	bool isAlphaCutout() const { return m_is_alpha_cutout; }
	void enableAlphaCutout(bool enable) { m_is_alpha_cutout = enable; updateShaderCombination(); }
	bool isShadowReceiver() const { return m_is_shadow_receiver; }
	void enableShadowReceiving(bool enable) { m_is_shadow_receiver = enable; updateShaderCombination(); }

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

	Material(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
		, m_shader(NULL)
		, m_is_z_test(true)
		, m_is_backface_culling(true)
		, m_depth_func(DepthFunc::LEQUAL)
		, m_is_alpha_to_coverage(false)
		, m_is_alpha_cutout(false)
		, m_shader_combination(0)
		, m_is_shadow_receiver(true)
		, m_uniforms(allocator)
		, m_allocator(allocator)
		, m_texture_count(0)
	{ 
		for (int i = 0; i < MAX_TEXTURE_COUNT; ++i)
		{
			m_textures[i] = nullptr;
		}
		updateShaderCombination();
	}

	~Material();

private:
	virtual void onReady(void) override;
	virtual void doUnload(void) override;
	virtual void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) override;

	bool deserializeTexture(JsonSerializer& serializer, const char* material_dir);

private:
	void deserializeUniforms(JsonSerializer& serializer);
	void updateShaderCombination();

private:
	static const int MAX_TEXTURE_COUNT = 16;

	Shader*	m_shader;
	Texture* m_textures[MAX_TEXTURE_COUNT];
	int m_texture_count;
	Array<Uniform> m_uniforms;
	bool m_is_z_test;
	bool m_is_backface_culling;
	bool m_is_alpha_to_coverage;
	bool m_is_alpha_cutout;
	bool m_is_shadow_receiver;
	DepthFunc m_depth_func;
	uint32_t m_shader_combination;
	IAllocator& m_allocator;
};

} // ~namespace Lumix
